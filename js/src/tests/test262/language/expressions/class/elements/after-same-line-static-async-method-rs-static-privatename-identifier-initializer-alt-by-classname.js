// |reftest| skip -- class-static-fields-private,class-fields-public is not supported
// This file was procedurally generated from the following sources:
// - src/class-elements/rs-static-privatename-identifier-initializer-alt-by-classname.case
// - src/class-elements/productions/cls-expr-after-same-line-static-async-method.template
/*---
description: Valid Static PrivateName (field definitions after a static async method in the same line)
esid: prod-FieldDefinition
features: [class-static-fields-private, class, class-fields-public, async-functions]
flags: [generated, async]
includes: [propertyHelper.js]
info: |
    ClassElement :
      MethodDefinition
      static MethodDefinition
      FieldDefinition ;
      static FieldDefinition ;
      ;

    FieldDefinition :
      ClassElementName Initializer _opt

    ClassElementName :
      PropertyName
      PrivateName

    PrivateName ::
      # IdentifierName

    IdentifierName ::
      IdentifierStart
      IdentifierName IdentifierPart

    IdentifierStart ::
      UnicodeIDStart
      $
      _
      \ UnicodeEscapeSequence

    IdentifierPart::
      UnicodeIDContinue
      $
      \ UnicodeEscapeSequence
      <ZWNJ> <ZWJ>

    UnicodeIDStart::
      any Unicode code point with the Unicode property "ID_Start"

    UnicodeIDContinue::
      any Unicode code point with the Unicode property "ID_Continue"


    NOTE 3
    The sets of code points with Unicode properties "ID_Start" and
    "ID_Continue" include, respectively, the code points with Unicode
    properties "Other_ID_Start" and "Other_ID_Continue".

---*/


var C = class {
  static async m() { return 42; } static #$ = 1; static #_ = 1; static #\u{6F} = 1; static #℘ = 1; static #ZW_‌_NJ = 1; static #ZW_‍_J = 1;
  static $() {
    return C.#$;
  }
  static _() {
    return C.#_;
  }
  static \u{6F}() {
    return C.#\u{6F};
  }
  static ℘() {
    return C.#℘;
  }
  static ZW_‌_NJ() {
    return C.#ZW_‌_NJ;
  }
  static ZW_‍_J() {
    return C.#ZW_‍_J;
  }
}

var c = new C();

assert.sameValue(Object.hasOwnProperty.call(c, "m"), false);
assert.sameValue(Object.hasOwnProperty.call(C.prototype, "m"), false);

verifyProperty(C, "m", {
  enumerable: false,
  configurable: true,
  writable: true,
}, {restore: true});

C.m().then(function(v) {
  assert.sameValue(v, 42);

  function assertions() {
    // Cover $DONE handler for async cases.
    function $DONE(error) {
      if (error) {
        throw new Test262Error('Test262:AsyncTestFailure')
      }
    }
    assert.sameValue(C.$(), 1);
    assert.sameValue(C._(), 1);
    assert.sameValue(C.\u{6F}(), 1);
    assert.sameValue(C.℘(), 1);
    assert.sameValue(C.ZW_‌_NJ(), 1);
    assert.sameValue(C.ZW_‍_J(), 1);

  }

  return Promise.resolve(assertions());
}, $DONE).then($DONE, $DONE);
