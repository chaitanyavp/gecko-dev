/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCopySupport.h"
#include "nsIDocumentEncoder.h"
#include "nsISupports.h"
#include "nsIContent.h"
#include "nsIComponentManager.h"
#include "nsIServiceManager.h"
#include "nsIClipboard.h"
#include "nsIFormControl.h"
#include "nsWidgetsCID.h"
#include "nsXPCOM.h"
#include "nsISupportsPrimitives.h"
#include "nsRange.h"
#include "imgIContainer.h"
#include "imgIRequest.h"
#include "nsIPresShell.h"
#include "nsFocusManager.h"
#include "mozilla/dom/DataTransfer.h"

#include "nsIDocShell.h"
#include "nsIContentViewerEdit.h"
#include "nsIClipboardHelper.h"
#include "nsISelectionController.h"

#include "nsPIDOMWindow.h"
#include "mozilla/dom/Document.h"
#include "nsIHTMLDocument.h"
#include "nsGkAtoms.h"
#include "nsIFrame.h"
#include "nsIURI.h"
#include "nsIURIMutator.h"
#include "nsISimpleEnumerator.h"

// image copy stuff
#include "nsIImageLoadingContent.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsContentUtils.h"
#include "nsContentCID.h"

#ifdef XP_WIN
#  include "nsCExternalHandlerService.h"
#  include "nsEscape.h"
#  include "nsIMIMEInfo.h"
#  include "nsIMIMEService.h"
#  include "nsIURL.h"
#  include "nsReadableUtils.h"
#  include "nsXULAppAPI.h"
#endif

#include "mozilla/ContentEvents.h"
#include "mozilla/dom/Element.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/IntegerRange.h"

using namespace mozilla;
using namespace mozilla::dom;

static NS_DEFINE_CID(kCClipboardCID, NS_CLIPBOARD_CID);
static NS_DEFINE_CID(kCTransferableCID, NS_TRANSFERABLE_CID);
static NS_DEFINE_CID(kHTMLConverterCID, NS_HTMLFORMATCONVERTER_CID);

// copy string data onto the transferable
static nsresult AppendString(nsITransferable* aTransferable,
                             const nsAString& aString, const char* aFlavor);

// copy HTML node data
static nsresult AppendDOMNode(nsITransferable* aTransferable,
                              nsINode* aDOMNode);

#ifdef XP_WIN
// copy image as file promise onto the transferable
static nsresult AppendImagePromise(nsITransferable* aTransferable,
                                   imgIRequest* aImgRequest,
                                   nsIImageLoadingContent* aImageElement);
#endif

// Helper used for HTMLCopy and GetTransferableForSelection since both routines
// share common code.
static nsresult SelectionCopyHelper(Selection* aSel, Document* aDoc,
                                    bool doPutOnClipboard, int16_t aClipboardID,
                                    uint32_t aFlags,
                                    nsITransferable** aTransferable) {
  // Clear the output parameter for the transferable, if provided.
  if (aTransferable) {
    *aTransferable = nullptr;
  }

  nsresult rv;

  nsCOMPtr<nsIDocumentEncoder> docEncoder;
  docEncoder = do_createHTMLCopyEncoder();

  // note that we assign text/unicode as mime type, but in fact
  // nsHTMLCopyEncoder ignore it and use text/html or text/plain depending where
  // the selection is. if it is a selection into input/textarea element or in a
  // html content with pre-wrap style : text/plain. Otherwise text/html. see
  // nsHTMLCopyEncoder::SetSelection
  nsAutoString mimeType;
  mimeType.AssignLiteral(kUnicodeMime);

  // Do the first and potentially trial encoding as preformatted and raw.
  uint32_t flags = aFlags | nsIDocumentEncoder::OutputPreformatted |
                   nsIDocumentEncoder::OutputRaw |
                   nsIDocumentEncoder::OutputForPlainTextClipboardCopy;

  rv = docEncoder->Init(aDoc, mimeType, flags);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = docEncoder->SetSelection(aSel);
  NS_ENSURE_SUCCESS(rv, rv);

  // SetSelection set the mime type to text/plain if the selection is inside a
  // text widget.
  rv = docEncoder->GetMimeType(mimeType);
  NS_ENSURE_SUCCESS(rv, rv);
  bool selForcedTextPlain = mimeType.EqualsLiteral(kTextMime);

  nsAutoString buf;
  rv = docEncoder->EncodeToString(buf);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = docEncoder->GetMimeType(mimeType);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!selForcedTextPlain && mimeType.EqualsLiteral(kTextMime)) {
    // SetSelection and EncodeToString use this case to signal that text/plain
    // was forced because the document is either not an nsIHTMLDocument or it's
    // XHTML.  We want to pretty print XHTML but not non-nsIHTMLDocuments.
    nsCOMPtr<nsIHTMLDocument> htmlDoc = do_QueryInterface(aDoc);
    if (!htmlDoc) {
      selForcedTextPlain = true;
    }
  }

  // The mime type is ultimately text/html if the encoder successfully encoded
  // the selection as text/html.
  bool encodedTextHTML = mimeType.EqualsLiteral(kHTMLMime);

  // First, prepare the text/plain clipboard flavor.
  nsAutoString textPlainBuf;
  if (selForcedTextPlain) {
    // Nothing to do.  buf contains the final, preformatted, raw text/plain.
    textPlainBuf.Assign(buf);
  } else {
    // Redo the encoding, but this time use pretty printing.
    flags = nsIDocumentEncoder::OutputSelectionOnly |
            nsIDocumentEncoder::OutputAbsoluteLinks |
            nsIDocumentEncoder::SkipInvisibleContent |
            nsIDocumentEncoder::OutputDropInvisibleBreak |
            (aFlags & (nsIDocumentEncoder::OutputNoScriptContent |
                       nsIDocumentEncoder::OutputRubyAnnotation));

    mimeType.AssignLiteral(kTextMime);
    rv = docEncoder->Init(aDoc, mimeType, flags);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = docEncoder->SetSelection(aSel);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = docEncoder->EncodeToString(textPlainBuf);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Second, prepare the text/html flavor.
  nsAutoString textHTMLBuf;
  nsAutoString htmlParentsBuf;
  nsAutoString htmlInfoBuf;
  if (encodedTextHTML) {
    // Redo the encoding, but this time use the passed-in flags.
    // Don't allow wrapping of CJK strings.
    mimeType.AssignLiteral(kHTMLMime);
    rv = docEncoder->Init(
        aDoc, mimeType,
        aFlags | nsIDocumentEncoder::OutputDisallowLineBreaking);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = docEncoder->SetSelection(aSel);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = docEncoder->EncodeToStringWithContext(htmlParentsBuf, htmlInfoBuf,
                                               textHTMLBuf);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Get the Clipboard
  nsCOMPtr<nsIClipboard> clipboard;
  if (doPutOnClipboard) {
    clipboard = do_GetService(kCClipboardCID, &rv);
    if (NS_FAILED(rv)) return rv;
  }

  if ((doPutOnClipboard && clipboard) || aTransferable != nullptr) {
    // Create a transferable for putting data on the Clipboard
    nsCOMPtr<nsITransferable> trans = do_CreateInstance(kCTransferableCID);
    if (trans) {
      trans->Init(aDoc->GetLoadContext());
      if (encodedTextHTML) {
        // Set up a format converter so that clipboard flavor queries work.
        // This converter isn't really used for conversions.
        nsCOMPtr<nsIFormatConverter> htmlConverter =
            do_CreateInstance(kHTMLConverterCID);
        trans->SetConverter(htmlConverter);

        if (!textHTMLBuf.IsEmpty()) {
          // Add the html DataFlavor to the transferable
          rv = AppendString(trans, textHTMLBuf, kHTMLMime);
          NS_ENSURE_SUCCESS(rv, rv);
        }

        // Add the htmlcontext DataFlavor to the transferable
        // Even if parents is empty string, this flavor should
        // be attached to the transferable
        rv = AppendString(trans, htmlParentsBuf, kHTMLContext);
        NS_ENSURE_SUCCESS(rv, rv);

        if (!htmlInfoBuf.IsEmpty()) {
          // Add the htmlinfo DataFlavor to the transferable
          rv = AppendString(trans, htmlInfoBuf, kHTMLInfo);
          NS_ENSURE_SUCCESS(rv, rv);
        }

        if (!textPlainBuf.IsEmpty()) {
          // unicode text
          // Add the unicode DataFlavor to the transferable
          // If we didn't have this, then nsDataObj::GetData matches
          // text/unicode against the kURLMime flavour which is not desirable
          // (eg. when pasting into Notepad)
          rv = AppendString(trans, textPlainBuf, kUnicodeMime);
          NS_ENSURE_SUCCESS(rv, rv);
        }

        // Try and get source URI of the items that are being dragged
        nsIURI* uri = aDoc->GetDocumentURI();
        if (uri) {
          nsAutoCString spec;
          nsresult rv = uri->GetSpec(spec);
          NS_ENSURE_SUCCESS(rv, rv);
          if (!spec.IsEmpty()) {
            nsAutoString shortcut;
            AppendUTF8toUTF16(spec, shortcut);

            // Add the URL DataFlavor to the transferable. Don't use kURLMime,
            // as it will cause an unnecessary UniformResourceLocator to be
            // added which confuses some apps eg. Outlook 2000 - (See Bug
            // 315370). Don't use kURLDataMime, as it will cause a bogus 'url '
            // flavor to show up on the Mac clipboard, confusing other apps,
            // like Terminal (see bug 336012).
            rv = AppendString(trans, shortcut, kURLPrivateMime);
            NS_ENSURE_SUCCESS(rv, rv);
          }
        }
      } else {
        if (!textPlainBuf.IsEmpty()) {
          // Add the unicode DataFlavor to the transferable
          rv = AppendString(trans, textPlainBuf, kUnicodeMime);
          NS_ENSURE_SUCCESS(rv, rv);
        }
      }

      if (doPutOnClipboard && clipboard) {
        // put the transferable on the clipboard
        clipboard->SetData(trans, nullptr, aClipboardID);
      }

      // Return the transferable to the caller if requested.
      if (aTransferable != nullptr) {
        trans.swap(*aTransferable);
      }
    }
  }
  return rv;
}

nsresult nsCopySupport::HTMLCopy(Selection* aSel, Document* aDoc,
                                 int16_t aClipboardID,
                                 bool aWithRubyAnnotation) {
  uint32_t flags = nsIDocumentEncoder::SkipInvisibleContent;
  if (aWithRubyAnnotation) {
    flags |= nsIDocumentEncoder::OutputRubyAnnotation;
  }
  return SelectionCopyHelper(aSel, aDoc, true, aClipboardID, flags, nullptr);
}

nsresult nsCopySupport::ClearSelectionCache() {
  nsresult rv;
  nsCOMPtr<nsIClipboard> clipboard = do_GetService(kCClipboardCID, &rv);
  clipboard->EmptyClipboard(nsIClipboard::kSelectionCache);
  return rv;
}

nsresult nsCopySupport::GetTransferableForSelection(
    Selection* aSel, Document* aDoc, nsITransferable** aTransferable) {
  return SelectionCopyHelper(aSel, aDoc, false, 0,
                             nsIDocumentEncoder::SkipInvisibleContent,
                             aTransferable);
}

nsresult nsCopySupport::GetTransferableForNode(
    nsINode* aNode, Document* aDoc, nsITransferable** aTransferable) {
  // Make a temporary selection with aNode in a single range.
  // XXX We should try to get rid of the Selection object here.
  // XXX bug 1245883
  RefPtr<Selection> selection = new Selection();
  RefPtr<nsRange> range = new nsRange(aNode);
  ErrorResult result;
  range->SelectNode(*aNode, result);
  if (NS_WARN_IF(result.Failed())) {
    return result.StealNSResult();
  }
  selection->AddRangeInternal(*range, aDoc, result);
  if (NS_WARN_IF(result.Failed())) {
    return result.StealNSResult();
  }
  // It's not the primary selection - so don't skip invisible content.
  uint32_t flags = 0;
  return SelectionCopyHelper(selection, aDoc, false, 0, flags, aTransferable);
}

nsresult nsCopySupport::GetContents(const nsACString& aMimeType,
                                    uint32_t aFlags, Selection* aSel,
                                    Document* aDoc, nsAString& outdata) {
  nsCOMPtr<nsIDocumentEncoder> docEncoder =
      do_createDocumentEncoder(PromiseFlatCString(aMimeType).get());
  NS_ENSURE_TRUE(docEncoder, NS_ERROR_FAILURE);

  uint32_t flags = aFlags | nsIDocumentEncoder::SkipInvisibleContent;

  if (aMimeType.EqualsLiteral("text/plain"))
    flags |= nsIDocumentEncoder::OutputPreformatted;

  NS_ConvertASCIItoUTF16 unicodeMimeType(aMimeType);

  nsresult rv = docEncoder->Init(aDoc, unicodeMimeType, flags);
  if (NS_FAILED(rv)) return rv;

  if (aSel) {
    rv = docEncoder->SetSelection(aSel);
    if (NS_FAILED(rv)) return rv;
  }

  // encode the selection
  return docEncoder->EncodeToString(outdata);
}

nsresult nsCopySupport::ImageCopy(nsIImageLoadingContent* aImageElement,
                                  nsILoadContext* aLoadContext,
                                  int32_t aCopyFlags) {
  nsresult rv;

  // create a transferable for putting data on the Clipboard
  nsCOMPtr<nsITransferable> trans(do_CreateInstance(kCTransferableCID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);
  trans->Init(aLoadContext);

  if (aCopyFlags & nsIContentViewerEdit::COPY_IMAGE_TEXT) {
    // get the location from the element
    nsCOMPtr<nsIURI> uri;
    rv = aImageElement->GetCurrentURI(getter_AddRefs(uri));
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_TRUE(uri, NS_ERROR_FAILURE);

    nsAutoCString location;
    rv = uri->GetSpec(location);
    NS_ENSURE_SUCCESS(rv, rv);

    // append the string to the transferable
    rv = AppendString(trans, NS_ConvertUTF8toUTF16(location), kUnicodeMime);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (aCopyFlags & nsIContentViewerEdit::COPY_IMAGE_HTML) {
    // append HTML data to the transferable
    nsCOMPtr<nsINode> node(do_QueryInterface(aImageElement, &rv));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = AppendDOMNode(trans, node);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (aCopyFlags & nsIContentViewerEdit::COPY_IMAGE_DATA) {
    // get the image data and its request from the element
    nsCOMPtr<imgIRequest> imgRequest;
    nsCOMPtr<imgIContainer> image = nsContentUtils::GetImageFromContent(
        aImageElement, getter_AddRefs(imgRequest));
    NS_ENSURE_TRUE(image, NS_ERROR_FAILURE);

#ifdef XP_WIN
    rv = AppendImagePromise(trans, imgRequest, aImageElement);
    NS_ENSURE_SUCCESS(rv, rv);
#endif

    // copy the image data onto the transferable
    rv = trans->SetTransferData(kNativeImageMime, image);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // get clipboard
  nsCOMPtr<nsIClipboard> clipboard(do_GetService(kCClipboardCID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  // check whether the system supports the selection clipboard or not.
  bool selectionSupported;
  rv = clipboard->SupportsSelectionClipboard(&selectionSupported);
  NS_ENSURE_SUCCESS(rv, rv);

  // put the transferable on the clipboard
  if (selectionSupported) {
    rv = clipboard->SetData(trans, nullptr, nsIClipboard::kSelectionClipboard);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return clipboard->SetData(trans, nullptr, nsIClipboard::kGlobalClipboard);
}

static nsresult AppendString(nsITransferable* aTransferable,
                             const nsAString& aString, const char* aFlavor) {
  nsresult rv;

  nsCOMPtr<nsISupportsString> data(
      do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = data->SetData(aString);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aTransferable->AddDataFlavor(aFlavor);
  NS_ENSURE_SUCCESS(rv, rv);

  return aTransferable->SetTransferData(aFlavor, data);
}

static nsresult AppendDOMNode(nsITransferable* aTransferable,
                              nsINode* aDOMNode) {
  nsresult rv;

  // serializer
  nsCOMPtr<nsIDocumentEncoder> docEncoder = do_createHTMLCopyEncoder();

  // get document for the encoder
  nsCOMPtr<Document> document = aDOMNode->OwnerDoc();

  // Note that XHTML is not counted as HTML here, because we can't copy it
  // properly (all the copy code for non-plaintext assumes using HTML
  // serializers and parsers is OK, and those mess up XHTML).
  DebugOnly<nsCOMPtr<nsIHTMLDocument>> htmlDoc =
      nsCOMPtr<nsIHTMLDocument>(do_QueryInterface(document, &rv));
  NS_ENSURE_SUCCESS(rv, NS_OK);

  NS_ENSURE_TRUE(document->IsHTMLDocument(), NS_OK);

  // init encoder with document and node
  rv =
      docEncoder->NativeInit(document, NS_LITERAL_STRING(kHTMLMime),
                             nsIDocumentEncoder::OutputAbsoluteLinks |
                                 nsIDocumentEncoder::OutputEncodeBasicEntities);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = docEncoder->SetNode(aDOMNode);
  NS_ENSURE_SUCCESS(rv, rv);

  // serialize to string
  nsAutoString html, context, info;
  rv = docEncoder->EncodeToStringWithContext(context, info, html);
  NS_ENSURE_SUCCESS(rv, rv);

  // copy them to the transferable
  if (!html.IsEmpty()) {
    rv = AppendString(aTransferable, html, kHTMLMime);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (!info.IsEmpty()) {
    rv = AppendString(aTransferable, info, kHTMLInfo);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // add a special flavor, even if we don't have html context data
  return AppendString(aTransferable, context, kHTMLContext);
}

#ifdef XP_WIN
static nsresult AppendImagePromise(nsITransferable* aTransferable,
                                   imgIRequest* aImgRequest,
                                   nsIImageLoadingContent* aImageElement) {
  nsresult rv;

  NS_ENSURE_TRUE(aImgRequest, NS_OK);

  uint32_t imageStatus;
  rv = aImgRequest->GetImageStatus(&imageStatus);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!(imageStatus & imgIRequest::STATUS_FRAME_COMPLETE) ||
      (imageStatus & imgIRequest::STATUS_ERROR)) {
    return NS_OK;
  }

  bool isMultipart;
  rv = aImgRequest->GetMultipart(&isMultipart);
  NS_ENSURE_SUCCESS(rv, rv);
  if (isMultipart) {
    return NS_OK;
  }

  nsCOMPtr<nsINode> node = do_QueryInterface(aImageElement, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Fix the file extension in the URL if necessary
  nsCOMPtr<nsIMIMEService> mimeService =
      do_GetService(NS_MIMESERVICE_CONTRACTID);
  NS_ENSURE_TRUE(mimeService, NS_OK);

  nsCOMPtr<nsIURI> imgUri;
  rv = aImgRequest->GetFinalURI(getter_AddRefs(imgUri));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURL> imgUrl = do_QueryInterface(imgUri);
  NS_ENSURE_TRUE(imgUrl, NS_OK);

  nsAutoCString extension;
  rv = imgUrl->GetFileExtension(extension);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCString mimeType;
  rv = aImgRequest->GetMimeType(getter_Copies(mimeType));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIMIMEInfo> mimeInfo;
  mimeService->GetFromTypeAndExtension(mimeType, EmptyCString(),
                                       getter_AddRefs(mimeInfo));
  NS_ENSURE_TRUE(mimeInfo, NS_OK);

  nsAutoCString spec;
  rv = imgUrl->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  // pass out the image source string
  nsString imageSourceString;
  CopyUTF8toUTF16(spec, imageSourceString);

  bool validExtension;
  if (extension.IsEmpty() ||
      NS_FAILED(mimeInfo->ExtensionExists(extension, &validExtension)) ||
      !validExtension) {
    // Fix the file extension in the URL
    nsAutoCString primaryExtension;
    mimeInfo->GetPrimaryExtension(primaryExtension);

    rv = NS_MutateURI(imgUri)
             .Apply(NS_MutatorMethod(&nsIURLMutator::SetFileExtension,
                                     primaryExtension, nullptr))
             .Finalize(imgUrl);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsAutoCString fileName;
  imgUrl->GetFileName(fileName);

  NS_UnescapeURL(fileName);

  // make the filename safe for the filesystem
  fileName.ReplaceChar(FILE_PATH_SEPARATOR FILE_ILLEGAL_CHARACTERS, '-');

  nsString imageDestFileName;
  CopyUTF8toUTF16(fileName, imageDestFileName);

  rv = AppendString(aTransferable, imageSourceString, kFilePromiseURLMime);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AppendString(aTransferable, imageDestFileName, kFilePromiseDestFilename);
  NS_ENSURE_SUCCESS(rv, rv);

  aTransferable->SetRequestingPrincipal(node->NodePrincipal());
  aTransferable->SetContentPolicyType(nsIContentPolicy::TYPE_INTERNAL_IMAGE);

  // add the dataless file promise flavor
  return aTransferable->AddDataFlavor(kFilePromiseMime);
}
#endif  // XP_WIN

nsIContent* nsCopySupport::GetSelectionForCopy(Document* aDocument,
                                               Selection** aSelection) {
  *aSelection = nullptr;

  nsIPresShell* presShell = aDocument->GetShell();
  if (!presShell) return nullptr;

  nsCOMPtr<nsIContent> focusedContent;
  nsCOMPtr<nsISelectionController> selectionController =
      presShell->GetSelectionControllerForFocusedContent(
          getter_AddRefs(focusedContent));
  if (!selectionController) {
    return nullptr;
  }

  RefPtr<Selection> sel = selectionController->GetSelection(
      nsISelectionController::SELECTION_NORMAL);
  sel.forget(aSelection);
  return focusedContent;
}

bool nsCopySupport::CanCopy(Document* aDocument) {
  if (!aDocument) return false;

  RefPtr<Selection> sel;
  GetSelectionForCopy(aDocument, getter_AddRefs(sel));
  NS_ENSURE_TRUE(sel, false);

  return !sel->IsCollapsed();
}

static bool IsInsideRuby(nsINode* aNode) {
  for (; aNode; aNode = aNode->GetParent()) {
    if (aNode->IsHTMLElement(nsGkAtoms::ruby)) {
      return true;
    }
  }
  return false;
}

static bool IsSelectionInsideRuby(Selection* aSelection) {
  uint32_t rangeCount = aSelection->RangeCount();
  ;
  for (auto i : IntegerRange(rangeCount)) {
    nsRange* range = aSelection->GetRangeAt(i);
    if (!IsInsideRuby(range->GetCommonAncestor())) {
      return false;
    }
  }
  return true;
}

static Element* GetElementOrNearestFlattenedTreeParentElement(nsINode* aNode) {
  if (!aNode->IsContent()) {
    return nullptr;
  }
  for (nsIContent* content = aNode->AsContent(); content;
       content = content->GetFlattenedTreeParent()) {
    if (content->IsElement()) {
      return content->AsElement();
    }
  }
  return nullptr;
}

bool nsCopySupport::FireClipboardEvent(EventMessage aEventMessage,
                                       int32_t aClipboardType,
                                       nsIPresShell* aPresShell,
                                       Selection* aSelection,
                                       bool* aActionTaken) {
  if (aActionTaken) {
    *aActionTaken = false;
  }

  EventMessage originalEventMessage = aEventMessage;
  if (originalEventMessage == ePasteNoFormatting) {
    originalEventMessage = ePaste;
  }

  NS_ASSERTION(originalEventMessage == eCut || originalEventMessage == eCopy ||
                   originalEventMessage == ePaste,
               "Invalid clipboard event type");

  nsCOMPtr<nsIPresShell> presShell = aPresShell;
  if (!presShell) return false;

  nsCOMPtr<Document> doc = presShell->GetDocument();
  if (!doc) return false;

  nsCOMPtr<nsPIDOMWindowOuter> piWindow = doc->GetWindow();
  if (!piWindow) return false;

  // Event target of clipboard events should be an element node which
  // contains selection start container.
  RefPtr<Element> targetElement;

  // If a selection was not supplied, try to find it.
  RefPtr<Selection> sel = aSelection;
  if (!sel) {
    GetSelectionForCopy(doc, getter_AddRefs(sel));
  }

  // Retrieve the event target node from the start of the selection.
  if (sel) {
    nsRange* range = sel->GetRangeAt(0);
    if (range) {
      targetElement = GetElementOrNearestFlattenedTreeParentElement(
          range->GetStartContainer());
    }
  }

  // If there is no selection ranges, use the <body> or <frameset> element.
  if (!targetElement) {
    targetElement = doc->GetBody();
    if (!targetElement) {
      return false;
    }
  }

  // It seems to be unsafe to fire an event handler during reflow (bug 393696)
  if (!nsContentUtils::IsSafeToRunScript()) {
    nsContentUtils::WarnScriptWasIgnored(doc);
    return false;
  }

  nsCOMPtr<nsIDocShell> docShell = piWindow->GetDocShell();
  const bool chromeShell =
      docShell && docShell->ItemType() == nsIDocShellTreeItem::typeChrome;

  // next, fire the cut, copy or paste event
  bool doDefault = true;
  RefPtr<DataTransfer> clipboardData;
  if (chromeShell ||
      Preferences::GetBool("dom.event.clipboardevents.enabled", true)) {
    clipboardData =
        new DataTransfer(doc->GetScopeObject(), aEventMessage,
                         originalEventMessage == ePaste, aClipboardType);

    nsEventStatus status = nsEventStatus_eIgnore;
    InternalClipboardEvent evt(true, originalEventMessage);
    evt.mClipboardData = clipboardData;
    EventDispatcher::Dispatch(targetElement, presShell->GetPresContext(), &evt,
                              nullptr, &status);
    // If the event was cancelled, don't do the clipboard operation
    doDefault = (status != nsEventStatus_eConsumeNoDefault);
  }

  // When this function exits, the event dispatch is over. We want to disconnect
  // our DataTransfer, which means setting its mode to `Protected` and clearing
  // all stored data, before we return.
  auto clearAfter = MakeScopeExit([&] {
    if (clipboardData) {
      clipboardData->Disconnect();

      // NOTE: Disconnect may not actually clear the DataTransfer if the
      // dom.events.dataTransfer.protected.enabled pref is not on, so we make
      // sure we clear here, as not clearing could provide the DataTransfer
      // access to information from the system clipboard at an arbitrary point
      // in the future.
      if (originalEventMessage == ePaste) {
        clipboardData->ClearAll();
      }
    }
  });

  // No need to do anything special during a paste. Either an event listener
  // took care of it and cancelled the event, or the caller will handle it.
  // Return true to indicate that the event wasn't cancelled.
  if (originalEventMessage == ePaste) {
    if (aActionTaken) {
      *aActionTaken = true;
    }
    return doDefault;
  }

  // Update the presentation in case the event handler modified the selection,
  // see bug 602231.
  presShell->FlushPendingNotifications(FlushType::Frames);
  if (presShell->IsDestroying()) return false;

  // if the event was not cancelled, do the default copy. If the event was
  // cancelled, use the data added to the data transfer and copy that instead.
  uint32_t count = 0;
  if (doDefault) {
    // find the focused node
    nsIContent* sourceContent = targetElement.get();
    if (targetElement->IsInNativeAnonymousSubtree()) {
      sourceContent = targetElement->FindFirstNonChromeOnlyAccessContent();
    }

    // check if we are looking at a password input
    nsCOMPtr<nsIFormControl> formControl = do_QueryInterface(sourceContent);
    if (formControl) {
      if (formControl->ControlType() == NS_FORM_INPUT_PASSWORD) {
        return false;
      }
    }

    // when cutting non-editable content, do nothing
    // XXX this is probably the wrong editable flag to check
    if (originalEventMessage != eCut || targetElement->IsEditable()) {
      // get the data from the selection if any
      if (sel->IsCollapsed()) {
        if (aActionTaken) {
          *aActionTaken = true;
        }
        return false;
      }
      // XXX Code which decides whether we should copy text with ruby
      // annotation is currenct depending on whether each range of the
      // selection is inside a same ruby container. But we really should
      // expose the full functionality in browser. See bug 1130891.
      bool withRubyAnnotation = IsSelectionInsideRuby(sel);
      // call the copy code
      nsresult rv = HTMLCopy(sel, doc, aClipboardType, withRubyAnnotation);
      if (NS_FAILED(rv)) {
        return false;
      }
    } else {
      return false;
    }
  } else if (clipboardData) {
    // check to see if any data was put on the data transfer.
    count = clipboardData->MozItemCount();
    if (count) {
      nsCOMPtr<nsIClipboard> clipboard(
          do_GetService("@mozilla.org/widget/clipboard;1"));
      NS_ENSURE_TRUE(clipboard, false);

      nsCOMPtr<nsITransferable> transferable =
          clipboardData->GetTransferable(0, doc->GetLoadContext());

      NS_ENSURE_TRUE(transferable, false);

      // put the transferable on the clipboard
      nsresult rv = clipboard->SetData(transferable, nullptr, aClipboardType);
      if (NS_FAILED(rv)) {
        return false;
      }
    }
  }

  // Now that we have copied, update the clipboard commands. This should have
  // the effect of updating the enabled state of the paste menu item.
  if (doDefault || count) {
    piWindow->UpdateCommands(NS_LITERAL_STRING("clipboard"), nullptr, 0);
  }

  if (aActionTaken) {
    *aActionTaken = true;
  }
  return doDefault;
}
