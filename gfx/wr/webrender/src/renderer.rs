/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! The high-level module responsible for interfacing with the GPU.
//!
//! Much of WebRender's design is driven by separating work into different
//! threads. To avoid the complexities of multi-threaded GPU access, we restrict
//! all communication with the GPU to one thread, the render thread. But since
//! issuing GPU commands is often a bottleneck, we move everything else (i.e.
//! the computation of what commands to issue) to another thread, the
//! RenderBackend thread. The RenderBackend, in turn, may delegate work to other
//! thread (like the SceneBuilder threads or Rayon workers), but the
//! Render-vs-RenderBackend distinction is the most important.
//!
//! The consumer is responsible for initializing the render thread before
//! calling into WebRender, which means that this module also serves as the
//! initial entry point into WebRender, and is responsible for spawning the
//! various other threads discussed above. That said, WebRender initialization
//! returns both the `Renderer` instance as well as a channel for communicating
//! directly with the `RenderBackend`. Aside from a few high-level operations
//! like 'render now', most of interesting commands from the consumer go over
//! that channel and operate on the `RenderBackend`.
//!
//! ## Space conversion guidelines
//! At this stage, we shuld be operating with `DevicePixel` and `FramebufferPixel` only.
//! "Framebuffer" space represents the final destination of our rendeing,
//! and it happens to be Y-flipped on OpenGL. The conversion is done as follows:
//!   - for rasterized primitives, the orthographics projection transforms
//! the content rectangle to -1 to 1
//!   - the viewport transformation is setup to map the whole range to
//! the framebuffer rectangle provided by the document view, stored in `DrawTarget`
//!   - all the direct framebuffer operations, like blitting, reading pixels, and setting
//! up the scissor, are accepting already transformed coordinates, which we can get by
//! calling `DrawTarget::to_framebuffer_rect`

use api::{BlobImageHandler, ColorF, ColorU};
use api::{DocumentId, Epoch, ExternalImageId};
use api::{ExternalImageType, FontRenderMode, FrameMsg, ImageFormat, PipelineId};
use api::{ImageRendering, Checkpoint, NotificationRequest};
use api::{DebugCommand, MemoryReport, VoidPtrToSizeFn};
use api::{RenderApiSender, RenderNotifier, TexelRect, TextureTarget};
use api::channel;
use api::units::*;
pub use api::DebugFlags;
use api::channel::PayloadReceiverHelperMethods;
use batch::{BatchKind, BatchTextures, BrushBatchKind, ClipBatchList};
#[cfg(any(feature = "capture", feature = "replay"))]
use capture::{CaptureConfig, ExternalCaptureImage, PlainExternalImage};
use debug_colors;
use debug_render::{DebugItem, DebugRenderer};
use device::{DepthFunction, Device, GpuFrameId, Program, UploadMethod, Texture, PBO};
use device::{DrawTarget, ExternalTexture, FBOId, ReadTarget, TextureSlot};
use device::{ShaderError, TextureFilter, TextureFlags,
             VertexUsageHint, VAO, VBO, CustomVAO};
use device::{ProgramCache, ReadPixelsFormat};
use device::query::GpuTimer;
use euclid::rect;
use euclid::Transform3D;
use frame_builder::{ChasePrimitive, FrameBuilderConfig};
use gleam::gl;
use glyph_rasterizer::{GlyphFormat, GlyphRasterizer};
use gpu_cache::{GpuBlockData, GpuCacheUpdate, GpuCacheUpdateList};
use gpu_cache::{GpuCacheDebugChunk, GpuCacheDebugCmd};
#[cfg(feature = "pathfinder")]
use gpu_glyph_renderer::GpuGlyphRenderer;
use gpu_types::ScalingInstance;
use internal_types::{TextureSource, ORTHO_FAR_PLANE, ORTHO_NEAR_PLANE, ResourceCacheError};
use internal_types::{CacheTextureId, DebugOutput, FastHashMap, LayerIndex, RenderedDocument, ResultMsg};
use internal_types::{TextureCacheAllocationKind, TextureCacheUpdate, TextureUpdateList, TextureUpdateSource};
use internal_types::{RenderTargetInfo, SavedTargetIndex};
use malloc_size_of::MallocSizeOfOps;
use picture::{RecordedDirtyRegion, TileCache};
use prim_store::DeferredResolve;
use profiler::{BackendProfileCounters, FrameProfileCounters, TimeProfileCounter,
               GpuProfileTag, RendererProfileCounters, RendererProfileTimers};
use profiler::{Profiler, ChangeIndicator};
use device::query::GpuProfiler;
use rayon::{ThreadPool, ThreadPoolBuilder};
use record::ApiRecordingReceiver;
use render_backend::{FrameId, RenderBackend};
use scene_builder::{SceneBuilder, LowPrioritySceneBuilder};
use shade::{Shaders, WrShaders};
use smallvec::SmallVec;
use render_task::RenderTaskTree;
use resource_cache::ResourceCache;
use util::drain_filter;

use std;
use std::cmp;
use std::collections::VecDeque;
use std::collections::hash_map::Entry;
use std::f32;
use std::mem;
use std::os::raw::c_void;
use std::path::PathBuf;
use std::rc::Rc;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{channel, Receiver};
use std::thread;
use std::cell::RefCell;
use texture_cache::TextureCache;
use thread_profiler::{register_thread_with_profiler, write_profile};
use tiling::{AlphaRenderTarget, ColorRenderTarget};
use tiling::{BlitJob, BlitJobSource, RenderPass, RenderPassKind, RenderTargetList};
use tiling::{Frame, RenderTarget, RenderTargetKind, TextureCacheRenderTarget};
#[cfg(not(feature = "pathfinder"))]
use tiling::GlyphJob;
use time::precise_time_ns;

cfg_if! {
    if #[cfg(feature = "debugger")] {
        use serde_json;
        use debug_server::{self, DebugServer};
    } else {
        use api::ApiMsg;
        use api::channel::MsgSender;
    }
}

/// Is only false if no WR instances have ever been created.
static HAS_BEEN_INITIALIZED: AtomicBool = AtomicBool::new(false);

/// Returns true if a WR instance has ever been initialized in this process.
pub fn wr_has_been_initialized() -> bool {
    HAS_BEEN_INITIALIZED.load(Ordering::SeqCst)
}

pub const MAX_VERTEX_TEXTURE_WIDTH: usize = 1024;
/// Enabling this toggle would force the GPU cache scattered texture to
/// be resized every frame, which enables GPU debuggers to see if this
/// is performed correctly.
const GPU_CACHE_RESIZE_TEST: bool = false;

/// Number of GPU blocks per UV rectangle provided for an image.
pub const BLOCKS_PER_UV_RECT: usize = 2;

const GPU_TAG_BRUSH_LINEAR_GRADIENT: GpuProfileTag = GpuProfileTag {
    label: "B_LinearGradient",
    color: debug_colors::POWDERBLUE,
};
const GPU_TAG_BRUSH_RADIAL_GRADIENT: GpuProfileTag = GpuProfileTag {
    label: "B_RadialGradient",
    color: debug_colors::LIGHTPINK,
};
const GPU_TAG_BRUSH_YUV_IMAGE: GpuProfileTag = GpuProfileTag {
    label: "B_YuvImage",
    color: debug_colors::DARKGREEN,
};
const GPU_TAG_BRUSH_MIXBLEND: GpuProfileTag = GpuProfileTag {
    label: "B_MixBlend",
    color: debug_colors::MAGENTA,
};
const GPU_TAG_BRUSH_BLEND: GpuProfileTag = GpuProfileTag {
    label: "B_Blend",
    color: debug_colors::ORANGE,
};
const GPU_TAG_BRUSH_IMAGE: GpuProfileTag = GpuProfileTag {
    label: "B_Image",
    color: debug_colors::SPRINGGREEN,
};
const GPU_TAG_BRUSH_SOLID: GpuProfileTag = GpuProfileTag {
    label: "B_Solid",
    color: debug_colors::RED,
};
const GPU_TAG_CACHE_CLIP: GpuProfileTag = GpuProfileTag {
    label: "C_Clip",
    color: debug_colors::PURPLE,
};
const GPU_TAG_CACHE_BORDER: GpuProfileTag = GpuProfileTag {
    label: "C_Border",
    color: debug_colors::CORNSILK,
};
const GPU_TAG_CACHE_LINE_DECORATION: GpuProfileTag = GpuProfileTag {
    label: "C_LineDecoration",
    color: debug_colors::YELLOWGREEN,
};
const GPU_TAG_SETUP_TARGET: GpuProfileTag = GpuProfileTag {
    label: "target init",
    color: debug_colors::SLATEGREY,
};
const GPU_TAG_SETUP_DATA: GpuProfileTag = GpuProfileTag {
    label: "data init",
    color: debug_colors::LIGHTGREY,
};
const GPU_TAG_PRIM_SPLIT_COMPOSITE: GpuProfileTag = GpuProfileTag {
    label: "SplitComposite",
    color: debug_colors::DARKBLUE,
};
const GPU_TAG_PRIM_TEXT_RUN: GpuProfileTag = GpuProfileTag {
    label: "TextRun",
    color: debug_colors::BLUE,
};
const GPU_TAG_BLUR: GpuProfileTag = GpuProfileTag {
    label: "Blur",
    color: debug_colors::VIOLET,
};
const GPU_TAG_BLIT: GpuProfileTag = GpuProfileTag {
    label: "Blit",
    color: debug_colors::LIME,
};
const GPU_TAG_SCALE: GpuProfileTag = GpuProfileTag {
    label: "Scale",
    color: debug_colors::GHOSTWHITE,
};

const GPU_SAMPLER_TAG_ALPHA: GpuProfileTag = GpuProfileTag {
    label: "Alpha Targets",
    color: debug_colors::BLACK,
};
const GPU_SAMPLER_TAG_OPAQUE: GpuProfileTag = GpuProfileTag {
    label: "Opaque Pass",
    color: debug_colors::BLACK,
};
const GPU_SAMPLER_TAG_TRANSPARENT: GpuProfileTag = GpuProfileTag {
    label: "Transparent Pass",
    color: debug_colors::BLACK,
};

/// The clear color used for the texture cache when the debug display is enabled.
/// We use a shade of blue so that we can still identify completely blue items in
/// the texture cache.
const TEXTURE_CACHE_DBG_CLEAR_COLOR: [f32; 4] = [0.0, 0.0, 0.8, 1.0];

impl BatchKind {
    #[cfg(feature = "debugger")]
    fn debug_name(&self) -> &'static str {
        match *self {
            BatchKind::SplitComposite => "SplitComposite",
            BatchKind::Brush(kind) => {
                match kind {
                    BrushBatchKind::Solid => "Brush (Solid)",
                    BrushBatchKind::Image(..) => "Brush (Image)",
                    BrushBatchKind::Blend => "Brush (Blend)",
                    BrushBatchKind::MixBlend { .. } => "Brush (Composite)",
                    BrushBatchKind::YuvImage(..) => "Brush (YuvImage)",
                    BrushBatchKind::RadialGradient => "Brush (RadialGradient)",
                    BrushBatchKind::LinearGradient => "Brush (LinearGradient)",
                }
            }
            BatchKind::TextRun(_) => "TextRun",
        }
    }

    fn sampler_tag(&self) -> GpuProfileTag {
        match *self {
            BatchKind::SplitComposite => GPU_TAG_PRIM_SPLIT_COMPOSITE,
            BatchKind::Brush(kind) => {
                match kind {
                    BrushBatchKind::Solid => GPU_TAG_BRUSH_SOLID,
                    BrushBatchKind::Image(..) => GPU_TAG_BRUSH_IMAGE,
                    BrushBatchKind::Blend => GPU_TAG_BRUSH_BLEND,
                    BrushBatchKind::MixBlend { .. } => GPU_TAG_BRUSH_MIXBLEND,
                    BrushBatchKind::YuvImage(..) => GPU_TAG_BRUSH_YUV_IMAGE,
                    BrushBatchKind::RadialGradient => GPU_TAG_BRUSH_RADIAL_GRADIENT,
                    BrushBatchKind::LinearGradient => GPU_TAG_BRUSH_LINEAR_GRADIENT,
                }
            }
            BatchKind::TextRun(_) => GPU_TAG_PRIM_TEXT_RUN,
        }
    }
}

fn flag_changed(before: DebugFlags, after: DebugFlags, select: DebugFlags) -> Option<bool> {
    if before & select != after & select {
        Some(after.contains(select))
    } else {
        None
    }
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub enum ShaderColorMode {
    FromRenderPassMode = 0,
    Alpha = 1,
    SubpixelConstantTextColor = 2,
    SubpixelWithBgColorPass0 = 3,
    SubpixelWithBgColorPass1 = 4,
    SubpixelWithBgColorPass2 = 5,
    SubpixelDualSource = 6,
    Bitmap = 7,
    ColorBitmap = 8,
    Image = 9,
}

impl From<GlyphFormat> for ShaderColorMode {
    fn from(format: GlyphFormat) -> ShaderColorMode {
        match format {
            GlyphFormat::Alpha | GlyphFormat::TransformedAlpha => ShaderColorMode::Alpha,
            GlyphFormat::Subpixel | GlyphFormat::TransformedSubpixel => {
                panic!("Subpixel glyph formats must be handled separately.");
            }
            GlyphFormat::Bitmap => ShaderColorMode::Bitmap,
            GlyphFormat::ColorBitmap => ShaderColorMode::ColorBitmap,
        }
    }
}

/// Enumeration of the texture samplers used across the various WebRender shaders.
///
/// Each variant corresponds to a uniform declared in shader source. We only bind
/// the variants we need for a given shader, so not every variant is bound for every
/// batch.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub(crate) enum TextureSampler {
    Color0,
    Color1,
    Color2,
    PrevPassAlpha,
    PrevPassColor,
    GpuCache,
    TransformPalette,
    RenderTasks,
    Dither,
    PrimitiveHeadersF,
    PrimitiveHeadersI,
}

impl TextureSampler {
    pub(crate) fn color(n: usize) -> TextureSampler {
        match n {
            0 => TextureSampler::Color0,
            1 => TextureSampler::Color1,
            2 => TextureSampler::Color2,
            _ => {
                panic!("There are only 3 color samplers.");
            }
        }
    }
}

impl Into<TextureSlot> for TextureSampler {
    fn into(self) -> TextureSlot {
        match self {
            TextureSampler::Color0 => TextureSlot(0),
            TextureSampler::Color1 => TextureSlot(1),
            TextureSampler::Color2 => TextureSlot(2),
            TextureSampler::PrevPassAlpha => TextureSlot(3),
            TextureSampler::PrevPassColor => TextureSlot(4),
            TextureSampler::GpuCache => TextureSlot(5),
            TextureSampler::TransformPalette => TextureSlot(6),
            TextureSampler::RenderTasks => TextureSlot(7),
            TextureSampler::Dither => TextureSlot(8),
            TextureSampler::PrimitiveHeadersF => TextureSlot(9),
            TextureSampler::PrimitiveHeadersI => TextureSlot(10),
        }
    }
}

#[derive(Debug, Clone, Copy)]
#[repr(C)]
pub struct PackedVertex {
    pub pos: [f32; 2],
}

pub(crate) mod desc {
    use device::{VertexAttribute, VertexAttributeKind, VertexDescriptor};

    pub const PRIM_INSTANCES: VertexDescriptor = VertexDescriptor {
        vertex_attributes: &[
            VertexAttribute {
                name: "aPosition",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
        ],
        instance_attributes: &[
            VertexAttribute {
                name: "aData",
                count: 4,
                kind: VertexAttributeKind::I32,
            },
        ],
    };

    pub const BLUR: VertexDescriptor = VertexDescriptor {
        vertex_attributes: &[
            VertexAttribute {
                name: "aPosition",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
        ],
        instance_attributes: &[
            VertexAttribute {
                name: "aBlurRenderTaskAddress",
                count: 1,
                kind: VertexAttributeKind::I32,
            },
            VertexAttribute {
                name: "aBlurSourceTaskAddress",
                count: 1,
                kind: VertexAttributeKind::I32,
            },
            VertexAttribute {
                name: "aBlurDirection",
                count: 1,
                kind: VertexAttributeKind::I32,
            },
        ],
    };

    pub const LINE: VertexDescriptor = VertexDescriptor {
        vertex_attributes: &[
            VertexAttribute {
                name: "aPosition",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
        ],
        instance_attributes: &[
            VertexAttribute {
                name: "aTaskRect",
                count: 4,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aLocalSize",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aWavyLineThickness",
                count: 1,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aStyle",
                count: 1,
                kind: VertexAttributeKind::I32,
            },
            VertexAttribute {
                name: "aOrientation",
                count: 1,
                kind: VertexAttributeKind::I32,
            },
        ],
    };

    pub const BORDER: VertexDescriptor = VertexDescriptor {
        vertex_attributes: &[
            VertexAttribute {
                name: "aPosition",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
        ],
        instance_attributes: &[
            VertexAttribute {
                name: "aTaskOrigin",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aRect",
                count: 4,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aColor0",
                count: 4,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aColor1",
                count: 4,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aFlags",
                count: 1,
                kind: VertexAttributeKind::I32,
            },
            VertexAttribute {
                name: "aWidths",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aRadii",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aClipParams1",
                count: 4,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aClipParams2",
                count: 4,
                kind: VertexAttributeKind::F32,
            },
        ],
    };

    pub const SCALE: VertexDescriptor = VertexDescriptor {
        vertex_attributes: &[
            VertexAttribute {
                name: "aPosition",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
        ],
        instance_attributes: &[
            VertexAttribute {
                name: "aScaleRenderTaskAddress",
                count: 1,
                kind: VertexAttributeKind::I32,
            },
            VertexAttribute {
                name: "aScaleSourceTaskAddress",
                count: 1,
                kind: VertexAttributeKind::I32,
            },
        ],
    };

    pub const CLIP: VertexDescriptor = VertexDescriptor {
        vertex_attributes: &[
            VertexAttribute {
                name: "aPosition",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
        ],
        instance_attributes: &[
            VertexAttribute {
                name: "aClipRenderTaskAddress",
                count: 1,
                kind: VertexAttributeKind::I32,
            },
            VertexAttribute {
                name: "aClipTransformId",
                count: 1,
                kind: VertexAttributeKind::I32,
            },
            VertexAttribute {
                name: "aPrimTransformId",
                count: 1,
                kind: VertexAttributeKind::I32,
            },
            VertexAttribute {
                name: "aClipDataResourceAddress",
                count: 4,
                kind: VertexAttributeKind::U16,
            },
            VertexAttribute {
                name: "aClipLocalPos",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aClipTileRect",
                count: 4,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aClipDeviceArea",
                count: 4,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aClipSnapOffsets",
                count: 4,
                kind: VertexAttributeKind::F32,
            }
        ],
    };

    pub const GPU_CACHE_UPDATE: VertexDescriptor = VertexDescriptor {
        vertex_attributes: &[
            VertexAttribute {
                name: "aPosition",
                count: 2,
                kind: VertexAttributeKind::U16Norm,
            },
            VertexAttribute {
                name: "aValue",
                count: 4,
                kind: VertexAttributeKind::F32,
            },
        ],
        instance_attributes: &[],
    };

    pub const VECTOR_STENCIL: VertexDescriptor = VertexDescriptor {
        vertex_attributes: &[
            VertexAttribute {
                name: "aPosition",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
        ],
        instance_attributes: &[
            VertexAttribute {
                name: "aFromPosition",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aCtrlPosition",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aToPosition",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aFromNormal",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aCtrlNormal",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aToNormal",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
            VertexAttribute {
                name: "aPathID",
                count: 1,
                kind: VertexAttributeKind::U16,
            },
            VertexAttribute {
                name: "aPad",
                count: 1,
                kind: VertexAttributeKind::U16,
            },
        ],
    };

    pub const VECTOR_COVER: VertexDescriptor = VertexDescriptor {
        vertex_attributes: &[
            VertexAttribute {
                name: "aPosition",
                count: 2,
                kind: VertexAttributeKind::F32,
            },
        ],
        instance_attributes: &[
            VertexAttribute {
                name: "aTargetRect",
                count: 4,
                kind: VertexAttributeKind::I32,
            },
            VertexAttribute {
                name: "aStencilOrigin",
                count: 2,
                kind: VertexAttributeKind::I32,
            },
            VertexAttribute {
                name: "aSubpixel",
                count: 1,
                kind: VertexAttributeKind::U16,
            },
            VertexAttribute {
                name: "aPad",
                count: 1,
                kind: VertexAttributeKind::U16,
            },
        ],
    };
}

#[derive(Debug, Copy, Clone)]
pub(crate) enum VertexArrayKind {
    Primitive,
    Blur,
    Clip,
    VectorStencil,
    VectorCover,
    Border,
    Scale,
    LineDecoration,
}

#[derive(Clone, Debug, PartialEq)]
pub enum GraphicsApi {
    OpenGL,
}

#[derive(Clone, Debug)]
pub struct GraphicsApiInfo {
    pub kind: GraphicsApi,
    pub renderer: String,
    pub version: String,
}

#[derive(Copy, Clone, PartialEq, Eq, Hash, Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum ImageBufferKind {
    Texture2D = 0,
    TextureRect = 1,
    TextureExternal = 2,
    Texture2DArray = 3,
}

//TODO: those types are the same, so let's merge them
impl From<TextureTarget> for ImageBufferKind {
    fn from(target: TextureTarget) -> Self {
        match target {
            TextureTarget::Default => ImageBufferKind::Texture2D,
            TextureTarget::Rect => ImageBufferKind::TextureRect,
            TextureTarget::Array => ImageBufferKind::Texture2DArray,
            TextureTarget::External => ImageBufferKind::TextureExternal,
        }
    }
}

#[derive(Debug, Copy, Clone)]
pub enum RendererKind {
    Native,
    OSMesa,
}

#[derive(Debug)]
pub struct GpuProfile {
    pub frame_id: GpuFrameId,
    pub paint_time_ns: u64,
}

impl GpuProfile {
    fn new<T>(frame_id: GpuFrameId, timers: &[GpuTimer<T>]) -> GpuProfile {
        let mut paint_time_ns = 0;
        for timer in timers {
            paint_time_ns += timer.time_ns;
        }
        GpuProfile {
            frame_id,
            paint_time_ns,
        }
    }
}

#[derive(Debug)]
pub struct CpuProfile {
    pub frame_id: GpuFrameId,
    pub backend_time_ns: u64,
    pub composite_time_ns: u64,
    pub draw_calls: usize,
}

impl CpuProfile {
    fn new(
        frame_id: GpuFrameId,
        backend_time_ns: u64,
        composite_time_ns: u64,
        draw_calls: usize,
    ) -> CpuProfile {
        CpuProfile {
            frame_id,
            backend_time_ns,
            composite_time_ns,
            draw_calls,
        }
    }
}

#[cfg(not(feature = "pathfinder"))]
pub struct GpuGlyphRenderer;

#[cfg(not(feature = "pathfinder"))]
impl GpuGlyphRenderer {
    fn new(_: &mut Device, _: &VAO, _: ShaderPrecacheFlags) -> Result<GpuGlyphRenderer, RendererError> {
        Ok(GpuGlyphRenderer)
    }
}

#[cfg(not(feature = "pathfinder"))]
struct StenciledGlyphPage;

/// A Texture that has been initialized by the `device` module and is ready to
/// be used.
struct ActiveTexture {
    texture: Texture,
    saved_index: Option<SavedTargetIndex>,
}

/// Helper struct for resolving device Textures for use during rendering passes.
///
/// Manages the mapping between the at-a-distance texture handles used by the
/// `RenderBackend` (which does not directly interface with the GPU) and actual
/// device texture handles.
struct TextureResolver {
    /// A map to resolve texture cache IDs to native textures.
    texture_cache_map: FastHashMap<CacheTextureId, Texture>,

    /// Map of external image IDs to native textures.
    external_images: FastHashMap<(ExternalImageId, u8), ExternalTexture>,

    /// A special 1x1 dummy texture used for shaders that expect to work with
    /// the output of the previous pass but are actually running in the first
    /// pass.
    dummy_cache_texture: Texture,

    /// The outputs of the previous pass, if applicable.
    prev_pass_color: Option<ActiveTexture>,
    prev_pass_alpha: Option<ActiveTexture>,

    /// Saved render targets from previous passes. This is used when a pass
    /// needs access to the result of a pass other than the immediately-preceding
    /// one. In this case, the `RenderTask` will get a a non-`None` `saved_index`,
    /// which will cause the resulting render target to be persisted in this list
    /// (at that index) until the end of the frame.
    saved_targets: Vec<Texture>,

    /// Pool of idle render target textures ready for re-use.
    ///
    /// Naively, it would seem like we only ever need two pairs of (color,
    /// alpha) render targets: one for the output of the previous pass (serving
    /// as input to the current pass), and one for the output of the current
    /// pass. However, there are cases where the output of one pass is used as
    /// the input to multiple future passes. For example, drop-shadows draw the
    /// picture in pass X, then reference it in pass X+1 to create the blurred
    /// shadow, and pass the results of both X and X+1 to pass X+2 draw the
    /// actual content.
    ///
    /// See the comments in `allocate_target_texture` for more insight on why
    /// reuse is a win.
    render_target_pool: Vec<Texture>,
}

impl TextureResolver {
    fn new(device: &mut Device) -> TextureResolver {
        let dummy_cache_texture = device
            .create_texture(
                TextureTarget::Array,
                ImageFormat::BGRA8,
                1,
                1,
                TextureFilter::Linear,
                None,
                1,
            );

        TextureResolver {
            texture_cache_map: FastHashMap::default(),
            external_images: FastHashMap::default(),
            dummy_cache_texture,
            prev_pass_alpha: None,
            prev_pass_color: None,
            saved_targets: Vec::default(),
            render_target_pool: Vec::new(),
        }
    }

    fn deinit(self, device: &mut Device) {
        device.delete_texture(self.dummy_cache_texture);

        for (_id, texture) in self.texture_cache_map {
            device.delete_texture(texture);
        }

        for texture in self.render_target_pool {
            device.delete_texture(texture);
        }
    }

    fn begin_frame(&mut self) {
        assert!(self.prev_pass_color.is_none());
        assert!(self.prev_pass_alpha.is_none());
        assert!(self.saved_targets.is_empty());
    }

    fn end_frame(&mut self, device: &mut Device, frame_id: GpuFrameId) {
        // return the cached targets to the pool
        self.end_pass(device, None, None);
        // return the saved targets as well
        while let Some(target) = self.saved_targets.pop() {
            self.return_to_pool(device, target);
        }

        // GC the render target pool.
        //
        // We use a simple scheme whereby we drop any texture that hasn't been used
        // in the last 30 frames. This should generally prevent any sustained build-
        // up of unused textures, unless we don't generate frames for a long period.
        // This can happen when the window is minimized, and we probably want to
        // flush all the WebRender caches in that case [1].
        //
        // [1] https://bugzilla.mozilla.org/show_bug.cgi?id=1494099
        self.retain_targets(device, |texture| texture.used_recently(frame_id, 30));
    }

    /// Transfers ownership of a render target back to the pool.
    fn return_to_pool(&mut self, device: &mut Device, target: Texture) {
        device.invalidate_render_target(&target);
        self.render_target_pool.push(target);
    }

    /// Drops all targets from the render target pool that do not satisfy the predicate.
    pub fn retain_targets<F: Fn(&Texture) -> bool>(&mut self, device: &mut Device, f: F) {
        // We can't just use retain() because `Texture` requires manual cleanup.
        let mut tmp = SmallVec::<[Texture; 8]>::new();
        for target in self.render_target_pool.drain(..) {
            if f(&target) {
                tmp.push(target);
            } else {
                device.delete_texture(target);
            }
        }
        self.render_target_pool.extend(tmp);
    }

    fn end_pass(
        &mut self,
        device: &mut Device,
        a8_texture: Option<ActiveTexture>,
        rgba8_texture: Option<ActiveTexture>,
    ) {
        // If we have cache textures from previous pass, return them to the pool.
        // Also assign the pool index of those cache textures to last pass's index because this is
        // the result of last pass.
        // Note: the order here is important, needs to match the logic in `RenderPass::build()`.
        if let Some(at) = self.prev_pass_color.take() {
            if let Some(index) = at.saved_index {
                assert_eq!(self.saved_targets.len(), index.0);
                self.saved_targets.push(at.texture);
            } else {
                self.return_to_pool(device, at.texture);
            }
        }
        if let Some(at) = self.prev_pass_alpha.take() {
            if let Some(index) = at.saved_index {
                assert_eq!(self.saved_targets.len(), index.0);
                self.saved_targets.push(at.texture);
            } else {
                self.return_to_pool(device, at.texture);
            }
        }

        // We have another pass to process, make these textures available
        // as inputs to the next pass.
        self.prev_pass_color = rgba8_texture;
        self.prev_pass_alpha = a8_texture;
    }

    // Bind a source texture to the device.
    fn bind(&self, texture_id: &TextureSource, sampler: TextureSampler, device: &mut Device) {
        match *texture_id {
            TextureSource::Invalid => {}
            TextureSource::PrevPassAlpha => {
                let texture = match self.prev_pass_alpha {
                    Some(ref at) => &at.texture,
                    None => &self.dummy_cache_texture,
                };
                device.bind_texture(sampler, texture);
            }
            TextureSource::PrevPassColor => {
                let texture = match self.prev_pass_color {
                    Some(ref at) => &at.texture,
                    None => &self.dummy_cache_texture,
                };
                device.bind_texture(sampler, texture);
            }
            TextureSource::External(external_image) => {
                let texture = self.external_images
                    .get(&(external_image.id, external_image.channel_index))
                    .expect(&format!("BUG: External image should be resolved by now"));
                device.bind_external_texture(sampler, texture);
            }
            TextureSource::TextureCache(index) => {
                let texture = &self.texture_cache_map[&index];
                device.bind_texture(sampler, texture);
            }
            TextureSource::RenderTaskCache(saved_index) => {
                let texture = &self.saved_targets[saved_index.0];
                device.bind_texture(sampler, texture)
            }
        }
    }

    // Get the real (OpenGL) texture ID for a given source texture.
    // For a texture cache texture, the IDs are stored in a vector
    // map for fast access.
    fn resolve(&self, texture_id: &TextureSource) -> Option<&Texture> {
        match *texture_id {
            TextureSource::Invalid => None,
            TextureSource::PrevPassAlpha => Some(
                match self.prev_pass_alpha {
                    Some(ref at) => &at.texture,
                    None => &self.dummy_cache_texture,
                }
            ),
            TextureSource::PrevPassColor => Some(
                match self.prev_pass_color {
                    Some(ref at) => &at.texture,
                    None => &self.dummy_cache_texture,
                }
            ),
            TextureSource::External(..) => {
                panic!("BUG: External textures cannot be resolved, they can only be bound.");
            }
            TextureSource::TextureCache(index) => {
                Some(&self.texture_cache_map[&index])
            }
            TextureSource::RenderTaskCache(saved_index) => {
                Some(&self.saved_targets[saved_index.0])
            }
        }
    }

    fn report_memory(&self) -> MemoryReport {
        let mut report = MemoryReport::default();

        // We're reporting GPU memory rather than heap-allocations, so we don't
        // use size_of_op.
        for t in self.texture_cache_map.values() {
            report.texture_cache_textures += t.size_in_bytes();
        }
        for t in self.render_target_pool.iter() {
            report.render_target_textures += t.size_in_bytes();
        }

        report
    }
}

#[derive(Debug, Copy, Clone, PartialEq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum BlendMode {
    None,
    Alpha,
    PremultipliedAlpha,
    PremultipliedDestOut,
    SubpixelDualSource,
    SubpixelConstantTextColor(ColorF),
    SubpixelWithBgColor,
}

/// Tracks the state of each row in the GPU cache texture.
struct CacheRow {
    /// Mirrored block data on CPU for this row. We store a copy of
    /// the data on the CPU side to improve upload batching.
    cpu_blocks: Box<[GpuBlockData; MAX_VERTEX_TEXTURE_WIDTH]>,
    /// True if this row is dirty.
    is_dirty: bool,
}

impl CacheRow {
    fn new() -> Self {
        CacheRow {
            cpu_blocks: Box::new([GpuBlockData::EMPTY; MAX_VERTEX_TEXTURE_WIDTH]),
            is_dirty: false,
        }
    }
}

/// The bus over which CPU and GPU versions of the GPU cache
/// get synchronized.
enum GpuCacheBus {
    /// PBO-based updates, currently operate on a row granularity.
    /// Therefore, are subject to fragmentation issues.
    PixelBuffer {
        /// PBO used for transfers.
        buffer: PBO,
        /// Per-row data.
        rows: Vec<CacheRow>,
    },
    /// Shader-based scattering updates. Currently rendered by a set
    /// of points into the GPU texture, each carrying a `GpuBlockData`.
    Scatter {
        /// Special program to run the scattered update.
        program: Program,
        /// VAO containing the source vertex buffers.
        vao: CustomVAO,
        /// VBO for positional data, supplied as normalized `u16`.
        buf_position: VBO<[u16; 2]>,
        /// VBO for gpu block data.
        buf_value: VBO<GpuBlockData>,
        /// Currently stored block count.
        count: usize,
    },
}

/// The device-specific representation of the cache texture in gpu_cache.rs
struct GpuCacheTexture {
    texture: Option<Texture>,
    bus: GpuCacheBus,
}

impl GpuCacheTexture {

    /// Ensures that we have an appropriately-sized texture. Returns true if a
    /// new texture was created.
    fn ensure_texture(&mut self, device: &mut Device, height: i32) {
        // If we already have a texture that works, we're done.
        if self.texture.as_ref().map_or(false, |t| t.get_dimensions().height >= height) {
            if GPU_CACHE_RESIZE_TEST {
                // Special debug mode - resize the texture even though it's fine.
            } else {
                return;
            }
        }

        // Take the old texture, if any.
        let blit_source = self.texture.take();

        // Create the new texture.
        assert!(height >= 2, "Height is too small for ANGLE");
        let new_size = DeviceIntSize::new(MAX_VERTEX_TEXTURE_WIDTH as _, height);
        let rt_info = Some(RenderTargetInfo { has_depth: false });
        let mut texture = device.create_texture(
            TextureTarget::Default,
            ImageFormat::RGBAF32,
            new_size.width,
            new_size.height,
            TextureFilter::Nearest,
            rt_info,
            1,
        );

        // Blit the contents of the previous texture, if applicable.
        if let Some(blit_source) = blit_source {
            device.blit_renderable_texture(&mut texture, &blit_source);
            device.delete_texture(blit_source);
        }

        self.texture = Some(texture);
    }

    fn new(device: &mut Device, use_scatter: bool) -> Result<Self, RendererError> {
        let bus = if use_scatter {
            let program = device.create_program_linked(
                "gpu_cache_update",
                String::new(),
                &desc::GPU_CACHE_UPDATE,
            )?;
            let buf_position = device.create_vbo();
            let buf_value = device.create_vbo();
            //Note: the vertex attributes have to be supplied in the same order
            // as for program creation, but each assigned to a different stream.
            let vao = device.create_custom_vao(&[
                buf_position.stream_with(&desc::GPU_CACHE_UPDATE.vertex_attributes[0..1]),
                buf_value   .stream_with(&desc::GPU_CACHE_UPDATE.vertex_attributes[1..2]),
            ]);
            GpuCacheBus::Scatter {
                program,
                vao,
                buf_position,
                buf_value,
                count: 0,
            }
        } else {
            let buffer = device.create_pbo();
            GpuCacheBus::PixelBuffer {
                buffer,
                rows: Vec::new(),
            }
        };

        Ok(GpuCacheTexture {
            texture: None,
            bus,
        })
    }

    fn deinit(mut self, device: &mut Device) {
        if let Some(t) = self.texture.take() {
            device.delete_texture(t);
        }
        match self.bus {
            GpuCacheBus::PixelBuffer { buffer, ..} => {
                device.delete_pbo(buffer);
            }
            GpuCacheBus::Scatter { program, vao, buf_position, buf_value, ..} => {
                device.delete_program(program);
                device.delete_custom_vao(vao);
                device.delete_vbo(buf_position);
                device.delete_vbo(buf_value);
            }
        }
    }

    fn get_height(&self) -> i32 {
        self.texture.as_ref().map_or(0, |t| t.get_dimensions().height)
    }

    fn prepare_for_updates(
        &mut self,
        device: &mut Device,
        total_block_count: usize,
        max_height: i32,
    ) {
        self.ensure_texture(device, max_height);
        match self.bus {
            GpuCacheBus::PixelBuffer { .. } => {},
            GpuCacheBus::Scatter {
                ref mut buf_position,
                ref mut buf_value,
                ref mut count,
                ..
            } => {
                *count = 0;
                if total_block_count > buf_value.allocated_count() {
                    device.allocate_vbo(buf_position, total_block_count, VertexUsageHint::Stream);
                    device.allocate_vbo(buf_value,    total_block_count, VertexUsageHint::Stream);
                }
            }
        }
    }

    fn update(&mut self, device: &mut Device, updates: &GpuCacheUpdateList) {
        match self.bus {
            GpuCacheBus::PixelBuffer { ref mut rows, .. } => {
                for update in &updates.updates {
                    match *update {
                        GpuCacheUpdate::Copy {
                            block_index,
                            block_count,
                            address,
                        } => {
                            let row = address.v as usize;

                            // Ensure that the CPU-side shadow copy of the GPU cache data has enough
                            // rows to apply this patch.
                            while rows.len() <= row {
                                // Add a new row.
                                rows.push(CacheRow::new());
                            }

                            // This row is dirty (needs to be updated in GPU texture).
                            rows[row].is_dirty = true;

                            // Copy the blocks from the patch array in the shadow CPU copy.
                            let block_offset = address.u as usize;
                            let data = &mut rows[row].cpu_blocks;
                            for i in 0 .. block_count {
                                data[block_offset + i] = updates.blocks[block_index + i];
                            }
                        }
                    }
                }
            }
            GpuCacheBus::Scatter {
                ref buf_position,
                ref buf_value,
                ref mut count,
                ..
            } => {
                //TODO: re-use this heap allocation
                // Unused positions will be left as 0xFFFF, which translates to
                // (1.0, 1.0) in the vertex output position and gets culled out
                let mut position_data = vec![[!0u16; 2]; updates.blocks.len()];
                let size = self.texture.as_ref().unwrap().get_dimensions().to_usize();

                for update in &updates.updates {
                    match *update {
                        GpuCacheUpdate::Copy {
                            block_index,
                            block_count,
                            address,
                        } => {
                            // Convert the absolute texel position into normalized
                            let y = ((2*address.v as usize + 1) << 15) / size.height;
                            for i in 0 .. block_count {
                                let x = ((2*address.u as usize + 2*i + 1) << 15) / size.width;
                                position_data[block_index + i] = [x as _, y as _];
                            }
                        }
                    }
                }

                device.fill_vbo(buf_value, &updates.blocks, *count);
                device.fill_vbo(buf_position, &position_data, *count);
                *count += position_data.len();
            }
        }
    }

    fn flush(&mut self, device: &mut Device) -> usize {
        let texture = self.texture.as_ref().unwrap();
        match self.bus {
            GpuCacheBus::PixelBuffer { ref buffer, ref mut rows } => {
                let rows_dirty = rows
                    .iter()
                    .filter(|row| row.is_dirty)
                    .count();
                if rows_dirty == 0 {
                    return 0
                }

                let mut uploader = device.upload_texture(
                    texture,
                    buffer,
                    rows_dirty * MAX_VERTEX_TEXTURE_WIDTH,
                );

                for (row_index, row) in rows.iter_mut().enumerate() {
                    if !row.is_dirty {
                        continue;
                    }

                    let rect = DeviceIntRect::new(
                        DeviceIntPoint::new(0, row_index as i32),
                        DeviceIntSize::new(MAX_VERTEX_TEXTURE_WIDTH as i32, 1),
                    );

                    uploader.upload(rect, 0, None, &*row.cpu_blocks);

                    row.is_dirty = false;
                }

                rows_dirty
            }
            GpuCacheBus::Scatter { ref program, ref vao, count, .. } => {
                device.disable_depth();
                device.set_blend(false);
                device.bind_program(program);
                device.bind_custom_vao(vao);
                device.bind_draw_target(
                    DrawTarget::Texture {
                        texture,
                        layer: 0,
                        with_depth: false,
                    },
                );
                device.draw_nonindexed_points(0, count as _);
                0
            }
        }
    }
}

struct VertexDataTexture {
    texture: Option<Texture>,
    format: ImageFormat,
    pbo: PBO,
}

impl VertexDataTexture {
    fn new(
        device: &mut Device,
        format: ImageFormat,
    ) -> VertexDataTexture {
        let pbo = device.create_pbo();
        VertexDataTexture { texture: None, format, pbo }
    }

    /// Returns a borrow of the GPU texture. Panics if it hasn't been initialized.
    fn texture(&self) -> &Texture {
        self.texture.as_ref().unwrap()
    }

    /// Returns an estimate of the GPU memory consumed by this VertexDataTexture.
    fn size_in_bytes(&self) -> usize {
        self.texture.as_ref().map_or(0, |t| t.size_in_bytes())
    }

    fn update<T>(&mut self, device: &mut Device, data: &mut Vec<T>) {
        debug_assert!(mem::size_of::<T>() % 16 == 0);
        let texels_per_item = mem::size_of::<T>() / 16;
        let items_per_row = MAX_VERTEX_TEXTURE_WIDTH / texels_per_item;

        // Ensure we always end up with a texture when leaving this method.
        if data.is_empty() {
            if self.texture.is_some() {
                return;
            }
            data.push(unsafe { mem::uninitialized() });
        }

        // Extend the data array to be a multiple of the row size.
        // This ensures memory safety when the array is passed to
        // OpenGL to upload to the GPU.
        if items_per_row != 0 {
            while data.len() % items_per_row != 0 {
                data.push(unsafe { mem::uninitialized() });
            }
        }

        let width =
            (MAX_VERTEX_TEXTURE_WIDTH - (MAX_VERTEX_TEXTURE_WIDTH % texels_per_item)) as i32;
        let needed_height = (data.len() / items_per_row) as i32;
        let existing_height = self.texture.as_ref().map_or(0, |t| t.get_dimensions().height);

        // Create a new texture if needed.
        //
        // These textures are generally very small, which is why we don't bother
        // with incremental updates and just re-upload every frame. For most pages
        // they're one row each, and on stress tests like css-francine they end up
        // in the 6-14 range. So we size the texture tightly to what we need (usually
        // 1), and shrink it if the waste would be more than 10 rows. This helps
        // with memory overhead, especially because there are several instances of
        // these textures per Renderer.
        if needed_height > existing_height || needed_height + 10 < existing_height {
            // Drop the existing texture, if any.
            if let Some(t) = self.texture.take() {
                device.delete_texture(t);
            }

            let texture = device.create_texture(
                TextureTarget::Default,
                self.format,
                width,
                // Ensure height is at least two to work around
                // https://bugs.chromium.org/p/angleproject/issues/detail?id=3039
                needed_height.max(2),
                TextureFilter::Nearest,
                None,
                1,
            );
            self.texture = Some(texture);
        }

        let rect = DeviceIntRect::new(
            DeviceIntPoint::zero(),
            DeviceIntSize::new(width, needed_height),
        );
        device
            .upload_texture(self.texture(), &self.pbo, 0)
            .upload(rect, 0, None, data);
    }

    fn deinit(mut self, device: &mut Device) {
        device.delete_pbo(self.pbo);
        if let Some(t) = self.texture.take() {
            device.delete_texture(t);
        }
    }
}

struct FrameOutput {
    last_access: GpuFrameId,
    fbo_id: FBOId,
}

#[derive(PartialEq)]
struct TargetSelector {
    size: DeviceIntSize,
    num_layers: usize,
    format: ImageFormat,
}

struct LazyInitializedDebugRenderer {
    debug_renderer: Option<DebugRenderer>,
    failed: bool,
}

impl LazyInitializedDebugRenderer {
    pub fn new() -> Self {
        Self {
            debug_renderer: None,
            failed: false,
        }
    }

    pub fn get_mut<'a>(&'a mut self, device: &mut Device) -> Option<&'a mut DebugRenderer> {
        if self.failed {
            return None;
        }
        if self.debug_renderer.is_none() {
            match DebugRenderer::new(device) {
                Ok(renderer) => { self.debug_renderer = Some(renderer); }
                Err(_) => {
                    // The shader compilation code already logs errors.
                    self.failed = true;
                }
            }
        }

        self.debug_renderer.as_mut()
    }

    /// Returns mut ref to `DebugRenderer` if one already exists, otherwise returns `None`.
    pub fn try_get_mut<'a>(&'a mut self) -> Option<&'a mut DebugRenderer> {
        self.debug_renderer.as_mut()
    }

    pub fn deinit(self, device: &mut Device) {
        if let Some(debug_renderer) = self.debug_renderer {
            debug_renderer.deinit(device);
        }
    }
}

// NB: If you add more VAOs here, be sure to deinitialize them in
// `Renderer::deinit()` below.
pub struct RendererVAOs {
    prim_vao: VAO,
    blur_vao: VAO,
    clip_vao: VAO,
    border_vao: VAO,
    line_vao: VAO,
    scale_vao: VAO,
}

/// The renderer is responsible for submitting to the GPU the work prepared by the
/// RenderBackend.
///
/// We have a separate `Renderer` instance for each instance of WebRender (generally
/// one per OS window), and all instances share the same thread.
pub struct Renderer {
    result_rx: Receiver<ResultMsg>,
    debug_server: DebugServer,
    pub device: Device,
    pending_texture_updates: Vec<TextureUpdateList>,
    pending_gpu_cache_updates: Vec<GpuCacheUpdateList>,
    pending_gpu_cache_clear: bool,
    pending_shader_updates: Vec<PathBuf>,
    active_documents: Vec<(DocumentId, RenderedDocument)>,

    shaders: Rc<RefCell<Shaders>>,

    pub gpu_glyph_renderer: GpuGlyphRenderer,

    max_recorded_profiles: usize,

    clear_color: Option<ColorF>,
    enable_clear_scissor: bool,
    debug: LazyInitializedDebugRenderer,
    debug_flags: DebugFlags,
    backend_profile_counters: BackendProfileCounters,
    profile_counters: RendererProfileCounters,
    resource_upload_time: u64,
    gpu_cache_upload_time: u64,
    profiler: Profiler,
    new_frame_indicator: ChangeIndicator,
    new_scene_indicator: ChangeIndicator,
    slow_frame_indicator: ChangeIndicator,

    last_time: u64,

    pub gpu_profile: GpuProfiler<GpuProfileTag>,
    vaos: RendererVAOs,

    prim_header_f_texture: VertexDataTexture,
    prim_header_i_texture: VertexDataTexture,
    transforms_texture: VertexDataTexture,
    render_task_texture: VertexDataTexture,
    gpu_cache_texture: GpuCacheTexture,

    /// When the GPU cache debugger is enabled, we keep track of the live blocks
    /// in the GPU cache so that we can use them for the debug display. This
    /// member stores those live blocks, indexed by row.
    gpu_cache_debug_chunks: Vec<Vec<GpuCacheDebugChunk>>,

    gpu_cache_frame_id: FrameId,
    gpu_cache_overflow: bool,

    pipeline_info: PipelineInfo,

    // Manages and resolves source textures IDs to real texture IDs.
    texture_resolver: TextureResolver,

    // A PBO used to do asynchronous texture cache uploads.
    texture_cache_upload_pbo: PBO,

    dither_matrix_texture: Option<Texture>,

    /// Optional trait object that allows the client
    /// application to provide external buffers for image data.
    external_image_handler: Option<Box<ExternalImageHandler>>,

    /// Optional trait object that allows the client
    /// application to provide a texture handle to
    /// copy the WR output to.
    output_image_handler: Option<Box<OutputImageHandler>>,

    /// Optional function pointers for measuring memory used by a given
    /// heap-allocated pointer.
    size_of_ops: Option<MallocSizeOfOps>,

    // Currently allocated FBOs for output frames.
    output_targets: FastHashMap<u32, FrameOutput>,

    pub renderer_errors: Vec<RendererError>,

    /// List of profile results from previous frames. Can be retrieved
    /// via get_frame_profiles().
    cpu_profiles: VecDeque<CpuProfile>,
    gpu_profiles: VecDeque<GpuProfile>,

    /// Notification requests to be fulfilled after rendering.
    notifications: Vec<NotificationRequest>,

    framebuffer_size: Option<FramebufferIntSize>,

    /// A lazily created texture for the zoom debugging widget.
    zoom_debug_texture: Option<Texture>,

    /// The current mouse position. This is used for debugging
    /// functionality only, such as the debug zoom widget.
    cursor_position: DeviceIntPoint,

    #[cfg(feature = "capture")]
    read_fbo: FBOId,
    #[cfg(feature = "replay")]
    owned_external_images: FastHashMap<(ExternalImageId, u8), ExternalTexture>,
}

#[derive(Debug)]
pub enum RendererError {
    Shader(ShaderError),
    Thread(std::io::Error),
    Resource(ResourceCacheError),
    MaxTextureSize,
}

impl From<ShaderError> for RendererError {
    fn from(err: ShaderError) -> Self {
        RendererError::Shader(err)
    }
}

impl From<std::io::Error> for RendererError {
    fn from(err: std::io::Error) -> Self {
        RendererError::Thread(err)
    }
}

impl From<ResourceCacheError> for RendererError {
    fn from(err: ResourceCacheError) -> Self {
        RendererError::Resource(err)
    }
}

impl Renderer {
    /// Initializes WebRender and creates a `Renderer` and `RenderApiSender`.
    ///
    /// # Examples
    /// Initializes a `Renderer` with some reasonable values. For more information see
    /// [`RendererOptions`][rendereroptions].
    ///
    /// ```rust,ignore
    /// # use webrender::renderer::Renderer;
    /// # use std::path::PathBuf;
    /// let opts = webrender::RendererOptions {
    ///    device_pixel_ratio: 1.0,
    ///    resource_override_path: None,
    ///    enable_aa: false,
    /// };
    /// let (renderer, sender) = Renderer::new(opts);
    /// ```
    /// [rendereroptions]: struct.RendererOptions.html
    pub fn new(
        gl: Rc<gl::Gl>,
        notifier: Box<RenderNotifier>,
        mut options: RendererOptions,
        shaders: Option<&mut WrShaders>
    ) -> Result<(Self, RenderApiSender), RendererError> {
        HAS_BEEN_INITIALIZED.store(true, Ordering::SeqCst);

        let (api_tx, api_rx) = channel::msg_channel()?;
        let (payload_tx, payload_rx) = channel::payload_channel()?;
        let (result_tx, result_rx) = channel();
        let gl_type = gl.get_type();

        let debug_server = DebugServer::new(api_tx.clone());

        let mut device = Device::new(
            gl,
            options.resource_override_path.clone(),
            options.upload_method.clone(),
            options.cached_programs.take(),
        );

        let ext_dual_source_blending = !options.disable_dual_source_blending &&
            device.supports_extension("GL_ARB_blend_func_extended") &&
            device.supports_extension("GL_ARB_explicit_attrib_location");

        // 512 is the minimum that the texture cache can work with.
        const MIN_TEXTURE_SIZE: i32 = 512;
        if let Some(user_limit) = options.max_texture_size {
            assert!(user_limit >= MIN_TEXTURE_SIZE);
            device.clamp_max_texture_size(user_limit);
        }
        if device.max_texture_size() < MIN_TEXTURE_SIZE {
            // Broken GL contexts can return a max texture size of zero (See #1260).
            // Better to gracefully fail now than panic as soon as a texture is allocated.
            error!(
                "Device reporting insufficient max texture size ({})",
                device.max_texture_size()
            );
            return Err(RendererError::MaxTextureSize);
        }
        let max_texture_size = device.max_texture_size();
        let max_texture_layers = device.max_texture_layers();

        register_thread_with_profiler("Compositor".to_owned());

        device.begin_frame();

        let shaders = match shaders {
            Some(shaders) => Rc::clone(&shaders.shaders),
            None => Rc::new(RefCell::new(Shaders::new(&mut device, gl_type, &options)?)),
        };

        let backend_profile_counters = BackendProfileCounters::new();

        let dither_matrix_texture = if options.enable_dithering {
            let dither_matrix: [u8; 64] = [
                00,
                48,
                12,
                60,
                03,
                51,
                15,
                63,
                32,
                16,
                44,
                28,
                35,
                19,
                47,
                31,
                08,
                56,
                04,
                52,
                11,
                59,
                07,
                55,
                40,
                24,
                36,
                20,
                43,
                27,
                39,
                23,
                02,
                50,
                14,
                62,
                01,
                49,
                13,
                61,
                34,
                18,
                46,
                30,
                33,
                17,
                45,
                29,
                10,
                58,
                06,
                54,
                09,
                57,
                05,
                53,
                42,
                26,
                38,
                22,
                41,
                25,
                37,
                21,
            ];

            let mut texture = device.create_texture(
                TextureTarget::Default,
                ImageFormat::R8,
                8,
                8,
                TextureFilter::Nearest,
                None,
                1,
            );
            device.upload_texture_immediate(&texture, &dither_matrix);

            Some(texture)
        } else {
            None
        };

        let x0 = 0.0;
        let y0 = 0.0;
        let x1 = 1.0;
        let y1 = 1.0;

        let quad_indices: [u16; 6] = [0, 1, 2, 2, 1, 3];
        let quad_vertices = [
            PackedVertex { pos: [x0, y0] },
            PackedVertex { pos: [x1, y0] },
            PackedVertex { pos: [x0, y1] },
            PackedVertex { pos: [x1, y1] },
        ];

        let prim_vao = device.create_vao(&desc::PRIM_INSTANCES);
        device.bind_vao(&prim_vao);
        device.update_vao_indices(&prim_vao, &quad_indices, VertexUsageHint::Static);
        device.update_vao_main_vertices(&prim_vao, &quad_vertices, VertexUsageHint::Static);

        let gpu_glyph_renderer = try!(GpuGlyphRenderer::new(&mut device,
                                                            &prim_vao,
                                                            options.precache_flags));

        let blur_vao = device.create_vao_with_new_instances(&desc::BLUR, &prim_vao);
        let clip_vao = device.create_vao_with_new_instances(&desc::CLIP, &prim_vao);
        let border_vao = device.create_vao_with_new_instances(&desc::BORDER, &prim_vao);
        let scale_vao = device.create_vao_with_new_instances(&desc::SCALE, &prim_vao);
        let line_vao = device.create_vao_with_new_instances(&desc::LINE, &prim_vao);
        let texture_cache_upload_pbo = device.create_pbo();

        let texture_resolver = TextureResolver::new(&mut device);

        let prim_header_f_texture = VertexDataTexture::new(&mut device, ImageFormat::RGBAF32);
        let prim_header_i_texture = VertexDataTexture::new(&mut device, ImageFormat::RGBAI32);
        let transforms_texture = VertexDataTexture::new(&mut device, ImageFormat::RGBAF32);
        let render_task_texture = VertexDataTexture::new(&mut device, ImageFormat::RGBAF32);

        let gpu_cache_texture = GpuCacheTexture::new(
            &mut device,
            options.scatter_gpu_cache_updates,
        )?;

        device.end_frame();

        let backend_notifier = notifier.clone();

        let default_font_render_mode = match (options.enable_aa, options.enable_subpixel_aa) {
            (true, true) => FontRenderMode::Subpixel,
            (true, false) => FontRenderMode::Alpha,
            (false, _) => FontRenderMode::Mono,
        };

        let config = FrameBuilderConfig {
            default_font_render_mode,
            dual_source_blending_is_enabled: true,
            dual_source_blending_is_supported: ext_dual_source_blending,
            chase_primitive: options.chase_primitive,
            enable_picture_caching: options.enable_picture_caching,
            testing: options.testing,
            gpu_supports_fast_clears: options.gpu_supports_fast_clears,
        };

        let device_pixel_ratio = options.device_pixel_ratio;
        let debug_flags = options.debug_flags;
        let payload_rx_for_backend = payload_rx.to_mpsc_receiver();
        let size_of_op = options.size_of_op;
        let enclosing_size_of_op = options.enclosing_size_of_op;
        let make_size_of_ops =
            move || size_of_op.map(|o| MallocSizeOfOps::new(o, enclosing_size_of_op));
        let recorder = options.recorder;
        let thread_listener = Arc::new(options.thread_listener);
        let thread_listener_for_rayon_start = thread_listener.clone();
        let thread_listener_for_rayon_end = thread_listener.clone();
        let workers = options
            .workers
            .take()
            .unwrap_or_else(|| {
                let worker = ThreadPoolBuilder::new()
                    .thread_name(|idx|{ format!("WRWorker#{}", idx) })
                    .start_handler(move |idx| {
                        register_thread_with_profiler(format!("WRWorker#{}", idx));
                        if let Some(ref thread_listener) = *thread_listener_for_rayon_start {
                            thread_listener.thread_started(&format!("WRWorker#{}", idx));
                        }
                    })
                    .exit_handler(move |idx| {
                        if let Some(ref thread_listener) = *thread_listener_for_rayon_end {
                            thread_listener.thread_stopped(&format!("WRWorker#{}", idx));
                        }
                    })
                    .build();
                Arc::new(worker.unwrap())
            });
        let sampler = options.sampler;
        let namespace_alloc_by_client = options.namespace_alloc_by_client;

        let blob_image_handler = options.blob_image_handler.take();
        let thread_listener_for_render_backend = thread_listener.clone();
        let thread_listener_for_scene_builder = thread_listener.clone();
        let thread_listener_for_lp_scene_builder = thread_listener.clone();
        let scene_builder_hooks = options.scene_builder_hooks;
        let rb_thread_name = format!("WRRenderBackend#{}", options.renderer_id.unwrap_or(0));
        let scene_thread_name = format!("WRSceneBuilder#{}", options.renderer_id.unwrap_or(0));
        let lp_scene_thread_name = format!("WRSceneBuilderLP#{}", options.renderer_id.unwrap_or(0));
        let glyph_rasterizer = GlyphRasterizer::new(workers)?;

        let (scene_builder, scene_tx, scene_rx) = SceneBuilder::new(
            config,
            api_tx.clone(),
            scene_builder_hooks,
            make_size_of_ops(),
        );
        thread::Builder::new().name(scene_thread_name.clone()).spawn(move || {
            register_thread_with_profiler(scene_thread_name.clone());
            if let Some(ref thread_listener) = *thread_listener_for_scene_builder {
                thread_listener.thread_started(&scene_thread_name);
            }

            let mut scene_builder = scene_builder;
            scene_builder.run();

            if let Some(ref thread_listener) = *thread_listener_for_scene_builder {
                thread_listener.thread_stopped(&scene_thread_name);
            }
        })?;

        let low_priority_scene_tx = if options.support_low_priority_transactions {
            let (low_priority_scene_tx, low_priority_scene_rx) = channel();
            let lp_builder = LowPrioritySceneBuilder {
                rx: low_priority_scene_rx,
                tx: scene_tx.clone(),
                simulate_slow_ms: 0,
            };

            thread::Builder::new().name(lp_scene_thread_name.clone()).spawn(move || {
                register_thread_with_profiler(lp_scene_thread_name.clone());
                if let Some(ref thread_listener) = *thread_listener_for_lp_scene_builder {
                    thread_listener.thread_started(&lp_scene_thread_name);
                }

                let mut scene_builder = lp_builder;
                scene_builder.run();

                if let Some(ref thread_listener) = *thread_listener_for_lp_scene_builder {
                    thread_listener.thread_stopped(&lp_scene_thread_name);
                }
            })?;

            low_priority_scene_tx
        } else {
            scene_tx.clone()
        };

        thread::Builder::new().name(rb_thread_name.clone()).spawn(move || {
            register_thread_with_profiler(rb_thread_name.clone());
            if let Some(ref thread_listener) = *thread_listener_for_render_backend {
                thread_listener.thread_started(&rb_thread_name);
            }

            let texture_cache = TextureCache::new(
                max_texture_size,
                max_texture_layers,
                TileCache::tile_dimensions(config.testing),
            );

            let resource_cache = ResourceCache::new(
                texture_cache,
                glyph_rasterizer,
                blob_image_handler,
            );

            let mut backend = RenderBackend::new(
                api_rx,
                payload_rx_for_backend,
                result_tx,
                scene_tx,
                low_priority_scene_tx,
                scene_rx,
                device_pixel_ratio,
                resource_cache,
                backend_notifier,
                config,
                recorder,
                sampler,
                make_size_of_ops(),
                debug_flags,
                namespace_alloc_by_client,
            );
            backend.run(backend_profile_counters);
            if let Some(ref thread_listener) = *thread_listener_for_render_backend {
                thread_listener.thread_stopped(&rb_thread_name);
            }
        })?;

        let ext_debug_marker = device.supports_extension("GL_EXT_debug_marker");
        let gpu_profile = GpuProfiler::new(Rc::clone(device.rc_gl()), ext_debug_marker);
        #[cfg(feature = "capture")]
        let read_fbo = device.create_fbo();

        let mut renderer = Renderer {
            result_rx,
            debug_server,
            device,
            active_documents: Vec::new(),
            pending_texture_updates: Vec::new(),
            pending_gpu_cache_updates: Vec::new(),
            pending_gpu_cache_clear: false,
            pending_shader_updates: Vec::new(),
            shaders,
            debug: LazyInitializedDebugRenderer::new(),
            debug_flags: DebugFlags::empty(),
            backend_profile_counters: BackendProfileCounters::new(),
            profile_counters: RendererProfileCounters::new(),
            resource_upload_time: 0,
            gpu_cache_upload_time: 0,
            profiler: Profiler::new(),
            new_frame_indicator: ChangeIndicator::new(),
            new_scene_indicator: ChangeIndicator::new(),
            slow_frame_indicator: ChangeIndicator::new(),
            max_recorded_profiles: options.max_recorded_profiles,
            clear_color: options.clear_color,
            enable_clear_scissor: options.enable_clear_scissor,
            last_time: 0,
            gpu_profile,
            gpu_glyph_renderer,
            vaos: RendererVAOs {
                prim_vao,
                blur_vao,
                clip_vao,
                border_vao,
                scale_vao,
                line_vao,
            },
            transforms_texture,
            prim_header_i_texture,
            prim_header_f_texture,
            render_task_texture,
            pipeline_info: PipelineInfo::default(),
            dither_matrix_texture,
            external_image_handler: None,
            output_image_handler: None,
            size_of_ops: make_size_of_ops(),
            output_targets: FastHashMap::default(),
            cpu_profiles: VecDeque::new(),
            gpu_profiles: VecDeque::new(),
            gpu_cache_texture,
            gpu_cache_debug_chunks: Vec::new(),
            gpu_cache_frame_id: FrameId::INVALID,
            gpu_cache_overflow: false,
            texture_cache_upload_pbo,
            texture_resolver,
            renderer_errors: Vec::new(),
            #[cfg(feature = "capture")]
            read_fbo,
            #[cfg(feature = "replay")]
            owned_external_images: FastHashMap::default(),
            notifications: Vec::new(),
            framebuffer_size: None,
            zoom_debug_texture: None,
            cursor_position: DeviceIntPoint::zero(),
        };

        // We initially set the flags to default and then now call set_debug_flags
        // to ensure any potential transition when enabling a flag is run.
        renderer.set_debug_flags(debug_flags);

        let sender = RenderApiSender::new(api_tx, payload_tx);
        Ok((renderer, sender))
    }

    pub fn framebuffer_size(&self) -> Option<FramebufferIntSize> {
        self.framebuffer_size
    }

    /// Update the current position of the debug cursor.
    pub fn set_cursor_position(
        &mut self,
        position: DeviceIntPoint,
    ) {
        self.cursor_position = position;
    }

    pub fn get_max_texture_size(&self) -> i32 {
        self.device.max_texture_size()
    }

    pub fn get_graphics_api_info(&self) -> GraphicsApiInfo {
        GraphicsApiInfo {
            kind: GraphicsApi::OpenGL,
            version: self.device.gl().get_string(gl::VERSION),
            renderer: self.device.gl().get_string(gl::RENDERER),
        }
    }

    /// Returns the Epoch of the current frame in a pipeline.
    pub fn current_epoch(&self, pipeline_id: PipelineId) -> Option<Epoch> {
        self.pipeline_info.epochs.get(&pipeline_id).cloned()
    }

    pub fn flush_pipeline_info(&mut self) -> PipelineInfo {
        mem::replace(&mut self.pipeline_info, PipelineInfo::default())
    }

    // update the program cache with new binaries, e.g. when some of the lazy loaded
    // shader programs got activated in the mean time
    pub fn update_program_cache(&mut self, cached_programs: Rc<ProgramCache>) {
        self.device.update_program_cache(cached_programs);
    }

    /// Processes the result queue.
    ///
    /// Should be called before `render()`, as texture cache updates are done here.
    pub fn update(&mut self) {
        profile_scope!("update");
        // Pull any pending results and return the most recent.
        while let Ok(msg) = self.result_rx.try_recv() {
            match msg {
                ResultMsg::PublishPipelineInfo(mut pipeline_info) => {
                    for (pipeline_id, epoch) in pipeline_info.epochs {
                        self.pipeline_info.epochs.insert(pipeline_id, epoch);
                    }
                    self.pipeline_info.removed_pipelines.extend(pipeline_info.removed_pipelines.drain(..));
                }
                ResultMsg::PublishDocument(
                    document_id,
                    mut doc,
                    texture_update_list,
                    profile_counters,
                ) => {
                    if doc.is_new_scene {
                        self.new_scene_indicator.changed();
                    }

                    // Add a new document to the active set, expressed as a `Vec` in order
                    // to re-order based on `DocumentLayer` during rendering.
                    match self.active_documents.iter().position(|&(id, _)| id == document_id) {
                        Some(pos) => {
                            // If the document we are replacing must be drawn
                            // (in order to update the texture cache), issue
                            // a render just to off-screen targets.
                            if self.active_documents[pos].1.frame.must_be_drawn() {
                                let framebuffer_size = self.framebuffer_size;
                                self.render_impl(framebuffer_size).ok();
                            }
                            self.active_documents[pos].1 = doc;
                        }
                        None => self.active_documents.push((document_id, doc)),
                    }

                    // IMPORTANT: The pending texture cache updates must be applied
                    //            *after* the previous frame has been rendered above
                    //            (if neceessary for a texture cache update). For
                    //            an example of why this is required:
                    //            1) Previous frame contains a render task that
                    //               targets Texture X.
                    //            2) New frame contains a texture cache update which
                    //               frees Texture X.
                    //            3) bad stuff happens.

                    //TODO: associate `document_id` with target window
                    self.pending_texture_updates.push(texture_update_list);
                    self.backend_profile_counters = profile_counters;
                }
                ResultMsg::UpdateGpuCache(mut list) => {
                    if list.clear {
                        self.pending_gpu_cache_clear = true;
                    }
                    if list.clear {
                        self.gpu_cache_debug_chunks = Vec::new();
                    }
                    for cmd in mem::replace(&mut list.debug_commands, Vec::new()) {
                        match cmd {
                            GpuCacheDebugCmd::Alloc(chunk) => {
                                let row = chunk.address.v as usize;
                                if row >= self.gpu_cache_debug_chunks.len() {
                                    self.gpu_cache_debug_chunks.resize(row + 1, Vec::new());
                                }
                                self.gpu_cache_debug_chunks[row].push(chunk);
                            },
                            GpuCacheDebugCmd::Free(address) => {
                                let chunks = &mut self.gpu_cache_debug_chunks[address.v as usize];
                                let pos = chunks.iter()
                                    .position(|x| x.address == address).unwrap();
                                chunks.remove(pos);
                            },
                        }
                    }
                    self.pending_gpu_cache_updates.push(list);
                }
                ResultMsg::UpdateResources {
                    updates,
                    memory_pressure,
                } => {
                    self.pending_texture_updates.push(updates);
                    self.device.begin_frame();

                    self.update_texture_cache();

                    // Flush the render target pool on memory pressure.
                    //
                    // This needs to be separate from the block below because
                    // the device module asserts if we delete textures while
                    // not in a frame.
                    if memory_pressure {
                        self.texture_resolver.retain_targets(&mut self.device, |_| false);
                    }

                    self.device.end_frame();
                    // If we receive a `PublishDocument` message followed by this one
                    // within the same update we need to cancel the frame because we
                    // might have deleted the resources in use in the frame due to a
                    // memory pressure event.
                    if memory_pressure {
                        self.active_documents.clear();
                    }
                }
                ResultMsg::AppendNotificationRequests(mut notifications) => {
                    if self.pending_texture_updates.is_empty() {
                        drain_filter(
                            &mut notifications,
                            |n| { n.when() == Checkpoint::FrameTexturesUpdated },
                            |n| { n.notify(); },
                        );
                    }
                    self.notifications.append(&mut notifications);
                }
                ResultMsg::RefreshShader(path) => {
                    self.pending_shader_updates.push(path);
                }
                ResultMsg::DebugOutput(output) => match output {
                    DebugOutput::FetchDocuments(string) |
                    DebugOutput::FetchClipScrollTree(string) => {
                        self.debug_server.send(string);
                    }
                    #[cfg(feature = "capture")]
                    DebugOutput::SaveCapture(config, deferred) => {
                        self.save_capture(config, deferred);
                    }
                    #[cfg(feature = "replay")]
                    DebugOutput::LoadCapture(root, plain_externals) => {
                        self.active_documents.clear();
                        self.load_capture(root, plain_externals);
                    }
                },
                ResultMsg::DebugCommand(command) => {
                    self.handle_debug_command(command);
                }
            }
        }
    }

    #[cfg(not(feature = "debugger"))]
    fn get_screenshot_for_debugger(&mut self) -> String {
        // Avoid unused param warning.
        let _ = &self.debug_server;
        String::new()
    }


    #[cfg(feature = "debugger")]
    fn get_screenshot_for_debugger(&mut self) -> String {
        use api::ImageDescriptor;

        let desc = ImageDescriptor::new(1024, 768, ImageFormat::BGRA8, true, false);
        let data = self.device.read_pixels(&desc);
        let screenshot = debug_server::Screenshot::new(desc.size, data);

        serde_json::to_string(&screenshot).unwrap()
    }

    #[cfg(not(feature = "debugger"))]
    fn get_passes_for_debugger(&self) -> String {
        // Avoid unused param warning.
        let _ = &self.debug_server;
        String::new()
    }

    #[cfg(feature = "debugger")]
    fn debug_alpha_target(target: &AlphaRenderTarget) -> debug_server::Target {
        let mut debug_target = debug_server::Target::new("A8");

        debug_target.add(
            debug_server::BatchKind::Cache,
            "Scalings",
            target.scalings.len(),
        );
        debug_target.add(
            debug_server::BatchKind::Cache,
            "Zero Clears",
            target.zero_clears.len(),
        );
        debug_target.add(
            debug_server::BatchKind::Cache,
            "One Clears",
            target.one_clears.len(),
        );
        debug_target.add(
            debug_server::BatchKind::Clip,
            "BoxShadows [p]",
            target.clip_batcher.primary_clips.box_shadows.len(),
        );
        debug_target.add(
            debug_server::BatchKind::Clip,
            "BoxShadows [s]",
            target.clip_batcher.secondary_clips.box_shadows.len(),
        );
        debug_target.add(
            debug_server::BatchKind::Cache,
            "Vertical Blur",
            target.vertical_blurs.len(),
        );
        debug_target.add(
            debug_server::BatchKind::Cache,
            "Horizontal Blur",
            target.horizontal_blurs.len(),
        );
        debug_target.add(
            debug_server::BatchKind::Clip,
            "Rectangles [p]",
            target.clip_batcher.primary_clips.rectangles.len(),
        );
        debug_target.add(
            debug_server::BatchKind::Clip,
            "Rectangles [s]",
            target.clip_batcher.secondary_clips.rectangles.len(),
        );
        for (_, items) in target.clip_batcher.primary_clips.images.iter() {
            debug_target.add(debug_server::BatchKind::Clip, "Image mask [p]", items.len());
        }
        for (_, items) in target.clip_batcher.secondary_clips.images.iter() {
            debug_target.add(debug_server::BatchKind::Clip, "Image mask [s]", items.len());
        }

        debug_target
    }

    #[cfg(feature = "debugger")]
    fn debug_color_target(target: &ColorRenderTarget) -> debug_server::Target {
        let mut debug_target = debug_server::Target::new("RGBA8");

        debug_target.add(
            debug_server::BatchKind::Cache,
            "Scalings",
            target.scalings.len(),
        );
        debug_target.add(
            debug_server::BatchKind::Cache,
            "Vertical Blur",
            target.vertical_blurs.len(),
        );
        debug_target.add(
            debug_server::BatchKind::Cache,
            "Horizontal Blur",
            target.horizontal_blurs.len(),
        );

        for alpha_batch_container in &target.alpha_batch_containers {
            for batch in alpha_batch_container.opaque_batches.iter().rev() {
                debug_target.add(
                    debug_server::BatchKind::Opaque,
                    batch.key.kind.debug_name(),
                    batch.instances.len(),
                );
            }

            for batch in &alpha_batch_container.alpha_batches {
                debug_target.add(
                    debug_server::BatchKind::Alpha,
                    batch.key.kind.debug_name(),
                    batch.instances.len(),
                );
            }
        }

        debug_target
    }

    #[cfg(feature = "debugger")]
    fn debug_texture_cache_target(target: &TextureCacheRenderTarget) -> debug_server::Target {
        let mut debug_target = debug_server::Target::new("Texture Cache");

        debug_target.add(
            debug_server::BatchKind::Cache,
            "Horizontal Blur",
            target.horizontal_blurs.len(),
        );

        debug_target
    }

    #[cfg(feature = "debugger")]
    fn get_passes_for_debugger(&self) -> String {
        let mut debug_passes = debug_server::PassList::new();

        for &(_, ref render_doc) in &self.active_documents {
            for pass in &render_doc.frame.passes {
                let mut debug_targets = Vec::new();
                match pass.kind {
                    RenderPassKind::MainFramebuffer(ref target) => {
                        debug_targets.push(Self::debug_color_target(target));
                    }
                    RenderPassKind::OffScreen { ref alpha, ref color, ref texture_cache } => {
                        debug_targets.extend(alpha.targets.iter().map(Self::debug_alpha_target));
                        debug_targets.extend(color.targets.iter().map(Self::debug_color_target));
                        debug_targets.extend(texture_cache.iter().map(|(_, target)| Self::debug_texture_cache_target(target)))
                    }
                }

                debug_passes.add(debug_server::Pass { targets: debug_targets });
            }
        }

        serde_json::to_string(&debug_passes).unwrap()
    }

    #[cfg(not(feature = "debugger"))]
    fn get_render_tasks_for_debugger(&self) -> String {
        String::new()
    }

    #[cfg(feature = "debugger")]
    fn get_render_tasks_for_debugger(&self) -> String {
        let mut debug_root = debug_server::RenderTaskList::new();

        for &(_, ref render_doc) in &self.active_documents {
            let debug_node = debug_server::TreeNode::new("document render tasks");
            let mut builder = debug_server::TreeNodeBuilder::new(debug_node);

            let render_tasks = &render_doc.frame.render_tasks;
            match render_tasks.tasks.last() {
                Some(main_task) => main_task.print_with(&mut builder, render_tasks),
                None => continue,
            };

            debug_root.add(builder.build());
        }

        serde_json::to_string(&debug_root).unwrap()
    }

    fn handle_debug_command(&mut self, command: DebugCommand) {
        match command {
            DebugCommand::EnableDualSourceBlending(_) => {
                panic!("Should be handled by render backend");
            }
            DebugCommand::FetchDocuments |
            DebugCommand::FetchClipScrollTree => {}
            DebugCommand::FetchRenderTasks => {
                let json = self.get_render_tasks_for_debugger();
                self.debug_server.send(json);
            }
            DebugCommand::FetchPasses => {
                let json = self.get_passes_for_debugger();
                self.debug_server.send(json);
            }
            DebugCommand::FetchScreenshot => {
                let json = self.get_screenshot_for_debugger();
                self.debug_server.send(json);
            }
            DebugCommand::SaveCapture(..) |
            DebugCommand::LoadCapture(..) => {
                panic!("Capture commands are not welcome here! Did you build with 'capture' feature?")
            }
            DebugCommand::ClearCaches(_)
            | DebugCommand::SimulateLongSceneBuild(_)
            | DebugCommand::SimulateLongLowPrioritySceneBuild(_) => {}
            DebugCommand::InvalidateGpuCache => {
                match self.gpu_cache_texture.bus {
                    GpuCacheBus::PixelBuffer { ref mut rows, .. } => {
                        info!("Invalidating GPU caches");
                        for row in rows {
                            row.is_dirty = true;
                        }
                    }
                    GpuCacheBus::Scatter { .. } => {
                        warn!("Unable to invalidate scattered GPU cache");
                    }
                }
            }
            DebugCommand::SetFlags(flags) => {
                self.set_debug_flags(flags);
            }
        }
    }

    /// Set a callback for handling external images.
    pub fn set_external_image_handler(&mut self, handler: Box<ExternalImageHandler>) {
        self.external_image_handler = Some(handler);
    }

    /// Set a callback for handling external outputs.
    pub fn set_output_image_handler(&mut self, handler: Box<OutputImageHandler>) {
        self.output_image_handler = Some(handler);
    }

    /// Retrieve (and clear) the current list of recorded frame profiles.
    pub fn get_frame_profiles(&mut self) -> (Vec<CpuProfile>, Vec<GpuProfile>) {
        let cpu_profiles = self.cpu_profiles.drain(..).collect();
        let gpu_profiles = self.gpu_profiles.drain(..).collect();
        (cpu_profiles, gpu_profiles)
    }

    /// Returns `true` if the active rendered documents (that need depth buffer)
    /// intersect on the main framebuffer, in which case we don't clear
    /// the whole depth and instead clear each document area separately.
    fn are_documents_intersecting_depth(&self) -> bool {
        let document_rects = self.active_documents
            .iter()
            .filter_map(|&(_, ref render_doc)| {
                match render_doc.frame.passes.last() {
                    Some(&RenderPass { kind: RenderPassKind::MainFramebuffer(ref target), .. })
                        if target.needs_depth() => Some(render_doc.frame.framebuffer_rect),
                    _ => None,
                }
            })
            .collect::<SmallVec<[_; 3]>>();

        for (i, rect) in document_rects.iter().enumerate() {
            for other in &document_rects[i+1 ..] {
                if rect.intersects(other) {
                    return true
                }
            }
        }

        false
    }

    pub fn notify_slow_frame(&mut self) {
        self.slow_frame_indicator.changed();
    }

    /// Renders the current frame.
    ///
    /// A Frame is supplied by calling [`generate_frame()`][webrender_api::Transaction::generate_frame].
    pub fn render(
        &mut self,
        framebuffer_size: FramebufferIntSize,
    ) -> Result<RenderResults, Vec<RendererError>> {
        self.framebuffer_size = Some(framebuffer_size);

        let result = self.render_impl(Some(framebuffer_size));

        drain_filter(
            &mut self.notifications,
            |n| { n.when() == Checkpoint::FrameRendered },
            |n| { n.notify(); },
        );

        // This is the end of the rendering pipeline. If some notifications are is still there,
        // just clear them and they will autimatically fire the Checkpoint::TransactionDropped
        // event. Otherwise they would just pile up in this vector forever.
        self.notifications.clear();

        result
    }

    // If framebuffer_size is None, don't render
    // to the main frame buffer. This is useful
    // to update texture cache render tasks but
    // avoid doing a full frame render.
    fn render_impl(
        &mut self,
        framebuffer_size: Option<FramebufferIntSize>,
    ) -> Result<RenderResults, Vec<RendererError>> {
        profile_scope!("render");
        let mut results = RenderResults::default();
        if self.active_documents.is_empty() {
            self.last_time = precise_time_ns();
            return Ok(results);
        }

        let mut frame_profiles = Vec::new();
        let mut profile_timers = RendererProfileTimers::new();

        // The texture resolver scope should be outside of any rendering, including
        // debug rendering. This ensures that when we return render targets to the
        // pool via glInvalidateFramebuffer, we don't do any debug rendering after
        // that point. Otherwise, the bind / invalidate / bind logic trips up the
        // render pass logic in tiled / mobile GPUs, resulting in an extra copy /
        // resolve step when the debug overlay is enabled.
        self.texture_resolver.begin_frame();

        let profile_samplers = {
            let _gm = self.gpu_profile.start_marker("build samples");
            // Block CPU waiting for last frame's GPU profiles to arrive.
            // In general this shouldn't block unless heavily GPU limited.
            let (gpu_frame_id, timers, samplers) = self.gpu_profile.build_samples();

            if self.max_recorded_profiles > 0 {
                while self.gpu_profiles.len() >= self.max_recorded_profiles {
                    self.gpu_profiles.pop_front();
                }
                self.gpu_profiles
                    .push_back(GpuProfile::new(gpu_frame_id, &timers));
            }
            profile_timers.gpu_samples = timers;
            samplers
        };


        let cpu_frame_id = profile_timers.cpu_time.profile(|| {
            let _gm = self.gpu_profile.start_marker("begin frame");
            let frame_id = self.device.begin_frame();
            self.gpu_profile.begin_frame(frame_id);

            self.device.disable_scissor();
            self.device.disable_depth();
            self.set_blend(false, FramebufferKind::Main);
            //self.update_shaders();

            self.update_texture_cache();

            frame_id
        });

        profile_timers.cpu_time.profile(|| {
            // If the documents don't intersect for depth, we can just do
            // a single, global depth clear.
            let clear_depth_per_doc = self.are_documents_intersecting_depth();

            //Note: another borrowck dance
            let mut active_documents = mem::replace(&mut self.active_documents, Vec::default());
            // sort by the document layer id
            active_documents.sort_by_key(|&(_, ref render_doc)| render_doc.frame.layer);

            #[cfg(feature = "replay")]
            self.texture_resolver.external_images.extend(
                self.owned_external_images.iter().map(|(key, value)| (*key, value.clone()))
            );

            for (doc_index, (_, RenderedDocument { ref mut frame, .. })) in active_documents.iter_mut().enumerate() {
                frame.profile_counters.reset_targets();
                self.prepare_gpu_cache(frame);
                assert!(frame.gpu_cache_frame_id <= self.gpu_cache_frame_id,
                    "Received frame depends on a later GPU cache epoch ({:?}) than one we received last via `UpdateGpuCache` ({:?})",
                    frame.gpu_cache_frame_id, self.gpu_cache_frame_id);

                // Work out what color to clear the frame buffer for this document.
                // The document's supplied clear color is used, unless:
                //  (a) The document has no specified clear color AND
                //  (b) We are rendering the first document.
                // If both those conditions are true, the overall renderer
                // clear color will be used, if specified.

                // Get the default clear color from the renderer.
                let mut fb_clear_color = if doc_index == 0 {
                    self.clear_color
                } else {
                    None
                };

                // Override with document clear color if no overall clear
                // color or not on the first document.
                if fb_clear_color.is_none() {
                    fb_clear_color = frame.background_color;
                }

                // Only clear the depth buffer for this document if this is
                // the first document, or we need to clear depth per document.
                let fb_clear_depth = if clear_depth_per_doc || doc_index == 0 {
                    Some(1.0)
                } else {
                    None
                };

                self.draw_tile_frame(
                    frame,
                    framebuffer_size,
                    cpu_frame_id,
                    &mut results.stats,
                    fb_clear_color,
                    fb_clear_depth,
                );

                if let Some(_) = framebuffer_size {
                    self.draw_frame_debug_items(&frame.debug_items);
                }
                if self.debug_flags.contains(DebugFlags::PROFILER_DBG) {
                    frame_profiles.push(frame.profile_counters.clone());
                }

                let dirty_regions =
                    mem::replace(&mut frame.recorded_dirty_regions, Vec::new());
                results.recorded_dirty_regions.extend(dirty_regions);
            }

            self.unlock_external_images();
            self.active_documents = active_documents;
        });

        if let Some(framebuffer_size) = framebuffer_size {
            self.draw_render_target_debug(framebuffer_size);
            self.draw_texture_cache_debug(framebuffer_size);
            self.draw_gpu_cache_debug(framebuffer_size);
            self.draw_zoom_debug(framebuffer_size);
            self.draw_epoch_debug();
        }

        let current_time = precise_time_ns();
        if framebuffer_size.is_some() {
            let ns = current_time - self.last_time;
            self.profile_counters.frame_time.set(ns);
        }

        if self.max_recorded_profiles > 0 {
            while self.cpu_profiles.len() >= self.max_recorded_profiles {
                self.cpu_profiles.pop_front();
            }
            let cpu_profile = CpuProfile::new(
                cpu_frame_id,
                self.backend_profile_counters.total_time.get(),
                profile_timers.cpu_time.get(),
                self.profile_counters.draw_calls.get(),
            );
            self.cpu_profiles.push_back(cpu_profile);
        }

        if self.debug_flags.contains(DebugFlags::PROFILER_DBG) {
            if let Some(framebuffer_size) = framebuffer_size {
                //TODO: take device/pixel ratio into equation?
                if let Some(debug_renderer) = self.debug.get_mut(&mut self.device) {
                    let screen_fraction = 1.0 / framebuffer_size.to_f32().area();
                    self.profiler.draw_profile(
                        &frame_profiles,
                        &self.backend_profile_counters,
                        &self.profile_counters,
                        &mut profile_timers,
                        &profile_samplers,
                        screen_fraction,
                        debug_renderer,
                        self.debug_flags.contains(DebugFlags::COMPACT_PROFILER),
                    );
                }
            }
        }

        let mut x = 0.0;
        if self.debug_flags.contains(DebugFlags::NEW_FRAME_INDICATOR) {
            if let Some(debug_renderer) = self.debug.get_mut(&mut self.device) {
                self.new_frame_indicator.changed();
                self.new_frame_indicator.draw(
                    x, 0.0,
                    ColorU::new(0, 110, 220, 255),
                    debug_renderer,
                );
                x += ChangeIndicator::width();
            }
        }

        if self.debug_flags.contains(DebugFlags::NEW_SCENE_INDICATOR) {
            if let Some(debug_renderer) = self.debug.get_mut(&mut self.device) {
                self.new_scene_indicator.draw(
                    x, 0.0,
                    ColorU::new(0, 220, 110, 255),
                    debug_renderer,
                );
                x += ChangeIndicator::width();
            }
        }

        if self.debug_flags.contains(DebugFlags::SLOW_FRAME_INDICATOR) {
            if let Some(debug_renderer) = self.debug.get_mut(&mut self.device) {
                self.slow_frame_indicator.draw(
                    x, 0.0,
                    ColorU::new(220, 30, 10, 255),
                    debug_renderer,
                );
            }
        }

        if self.debug_flags.contains(DebugFlags::ECHO_DRIVER_MESSAGES) {
            Device::echo_driver_messages(self.device.gl());
        }

        results.stats.texture_upload_kb = self.profile_counters.texture_data_uploaded.get();
        self.backend_profile_counters.reset();
        self.profile_counters.reset();
        self.profile_counters.frame_counter.inc();
        results.stats.resource_upload_time = self.resource_upload_time;
        self.resource_upload_time = 0;
        results.stats.gpu_cache_upload_time = self.gpu_cache_upload_time;
        self.gpu_cache_upload_time = 0;

        profile_timers.cpu_time.profile(|| {
            let _gm = self.gpu_profile.start_marker("end frame");
            self.gpu_profile.end_frame();
            if let Some(debug_renderer) = self.debug.try_get_mut() {
                debug_renderer.render(&mut self.device, framebuffer_size);
            }
            // See comment for texture_resolver.begin_frame() for explanation
            // of why this must be done after all rendering, including debug
            // overlays. The end_frame() call implicitly calls end_pass(), which
            // should ensure any left over render targets get invalidated and
            // returned to the pool correctly.
            self.texture_resolver.end_frame(&mut self.device, cpu_frame_id);
            self.device.end_frame();
        });
        if framebuffer_size.is_some() {
            self.last_time = current_time;
        }

        if self.renderer_errors.is_empty() {
            Ok(results)
        } else {
            Err(mem::replace(&mut self.renderer_errors, Vec::new()))
        }
    }

    fn update_gpu_cache(&mut self) {
        let _gm = self.gpu_profile.start_marker("gpu cache update");

        // For an artificial stress test of GPU cache resizing,
        // always pass an extra update list with at least one block in it.
        let gpu_cache_height = self.gpu_cache_texture.get_height();
        if gpu_cache_height != 0 && GPU_CACHE_RESIZE_TEST {
            self.pending_gpu_cache_updates.push(GpuCacheUpdateList {
                frame_id: FrameId::INVALID,
                clear: false,
                height: gpu_cache_height,
                blocks: vec![[1f32; 4].into()],
                updates: Vec::new(),
                debug_commands: Vec::new(),
            });
        }

        let (updated_blocks, max_requested_height) = self
            .pending_gpu_cache_updates
            .iter()
            .fold((0, gpu_cache_height), |(count, height), list| {
                (count + list.blocks.len(), cmp::max(height, list.height))
            });

        if max_requested_height > self.get_max_texture_size() && !self.gpu_cache_overflow {
            self.gpu_cache_overflow = true;
            self.renderer_errors.push(RendererError::MaxTextureSize);
        }

        // Note: if we decide to switch to scatter-style GPU cache update
        // permanently, we can have this code nicer with `BufferUploader` kind
        // of helper, similarly to how `TextureUploader` API is used.
        self.gpu_cache_texture.prepare_for_updates(
            &mut self.device,
            updated_blocks,
            max_requested_height,
        );

        for update_list in self.pending_gpu_cache_updates.drain(..) {
            assert!(update_list.height <= max_requested_height);
            if update_list.frame_id > self.gpu_cache_frame_id {
                self.gpu_cache_frame_id = update_list.frame_id
            }
            self.gpu_cache_texture
                .update(&mut self.device, &update_list);
        }

        let mut upload_time = TimeProfileCounter::new("GPU cache upload time", false);
        let updated_rows = upload_time.profile(|| {
            return self.gpu_cache_texture.flush(&mut self.device);
        });
        self.gpu_cache_upload_time += upload_time.get();

        let counters = &mut self.backend_profile_counters.resources.gpu_cache;
        counters.updated_rows.set(updated_rows);
        counters.updated_blocks.set(updated_blocks);
    }

    fn prepare_gpu_cache(&mut self, frame: &Frame) {
        if self.pending_gpu_cache_clear {
            let use_scatter =
                matches!(self.gpu_cache_texture.bus, GpuCacheBus::Scatter { .. });
            let new_cache = GpuCacheTexture::new(&mut self.device, use_scatter).unwrap();
            let old_cache = mem::replace(&mut self.gpu_cache_texture, new_cache);
            old_cache.deinit(&mut self.device);
            self.pending_gpu_cache_clear = false;
        }

        let deferred_update_list = self.update_deferred_resolves(&frame.deferred_resolves);
        self.pending_gpu_cache_updates.extend(deferred_update_list);

        self.update_gpu_cache();

        // Note: the texture might have changed during the `update`,
        // so we need to bind it here.
        self.device.bind_texture(
            TextureSampler::GpuCache,
            self.gpu_cache_texture.texture.as_ref().unwrap(),
        );
    }

    fn update_texture_cache(&mut self) {
        let _gm = self.gpu_profile.start_marker("texture cache update");
        let mut pending_texture_updates = mem::replace(&mut self.pending_texture_updates, vec![]);

        let mut upload_time = TimeProfileCounter::new("Resource upload time", false);
        upload_time.profile(|| {
            for update_list in pending_texture_updates.drain(..) {
                for allocation in update_list.allocations {
                    let is_realloc = matches!(allocation.kind, TextureCacheAllocationKind::Realloc(..));
                    match allocation.kind {
                        TextureCacheAllocationKind::Alloc(info) |
                        TextureCacheAllocationKind::Realloc(info) => {
                            // Create a new native texture, as requested by the texture cache.
                            //
                            // Ensure no PBO is bound when creating the texture storage,
                            // or GL will attempt to read data from there.
                            let mut texture = self.device.create_texture(
                                TextureTarget::Array,
                                info.format,
                                info.width,
                                info.height,
                                info.filter,
                                // This needs to be a render target because some render
                                // tasks get rendered into the texture cache.
                                Some(RenderTargetInfo { has_depth: false }),
                                info.layer_count,
                            );

                            if info.is_shared_cache {
                                texture.flags_mut()
                                    .insert(TextureFlags::IS_SHARED_TEXTURE_CACHE);

                                // Textures in the cache generally don't need to be cleared,
                                // but we do so if the debug display is active to make it
                                // easier to identify unallocated regions.
                                if self.debug_flags.contains(DebugFlags::TEXTURE_CACHE_DBG) {
                                    self.clear_texture(&texture, TEXTURE_CACHE_DBG_CLEAR_COLOR);
                                }
                            }

                            let old = self.texture_resolver.texture_cache_map.insert(allocation.id, texture);
                            assert_eq!(old.is_some(), is_realloc, "Renderer and RenderBackend disagree");
                            if let Some(old) = old {
                                self.device.blit_renderable_texture(
                                    self.texture_resolver.texture_cache_map.get_mut(&allocation.id).unwrap(),
                                    &old
                                );
                                self.device.delete_texture(old);
                            }
                        },
                        TextureCacheAllocationKind::Free => {
                            let texture = self.texture_resolver.texture_cache_map.remove(&allocation.id).unwrap();
                            self.device.delete_texture(texture);
                        },
                    }
                }

                for update in update_list.updates {
                    let TextureCacheUpdate { id, rect, stride, offset, layer_index, source } = update;
                    let texture = &self.texture_resolver.texture_cache_map[&id];

                    let bytes_uploaded = match source {
                        TextureUpdateSource::Bytes { data } => {
                            let mut uploader = self.device.upload_texture(
                                texture,
                                &self.texture_cache_upload_pbo,
                                0,
                            );
                            uploader.upload(
                                rect, layer_index, stride,
                                &data[offset as usize ..],
                            )
                        }
                        TextureUpdateSource::External { id, channel_index } => {
                            let mut uploader = self.device.upload_texture(
                                texture,
                                &self.texture_cache_upload_pbo,
                                0,
                            );
                            let handler = self.external_image_handler
                                .as_mut()
                                .expect("Found external image, but no handler set!");
                            // The filter is only relevant for NativeTexture external images.
                            let size = match handler.lock(id, channel_index, ImageRendering::Auto).source {
                                ExternalImageSource::RawData(data) => {
                                    uploader.upload(
                                        rect, layer_index, stride,
                                        &data[offset as usize ..],
                                    )
                                }
                                ExternalImageSource::Invalid => {
                                    // Create a local buffer to fill the pbo.
                                    let bpp = texture.get_format().bytes_per_pixel();
                                    let width = stride.unwrap_or(rect.size.width * bpp);
                                    let total_size = width * rect.size.height;
                                    // WR haven't support RGBAF32 format in texture_cache, so
                                    // we use u8 type here.
                                    let dummy_data: Vec<u8> = vec![255; total_size as usize];
                                    uploader.upload(rect, layer_index, stride, &dummy_data)
                                }
                                ExternalImageSource::NativeTexture(eid) => {
                                    panic!("Unexpected external texture {:?} for the texture cache update of {:?}", eid, id);
                                }
                            };
                            handler.unlock(id, channel_index);
                            size
                        }
                        TextureUpdateSource::DebugClear => {
                            let draw_target = DrawTarget::Texture {
                                texture,
                                layer: layer_index as usize,
                                with_depth: false,
                            };
                            self.device.bind_draw_target(draw_target);
                            self.device.clear_target(
                                Some(TEXTURE_CACHE_DBG_CLEAR_COLOR),
                                None,
                                Some(draw_target.to_framebuffer_rect(rect.to_i32()))
                            );
                            0
                        }
                    };
                    self.profile_counters.texture_data_uploaded.add(bytes_uploaded >> 10);
                }
            }

            drain_filter(
                &mut self.notifications,
                |n| { n.when() == Checkpoint::FrameTexturesUpdated },
                |n| { n.notify(); },
            );
        });
        self.resource_upload_time += upload_time.get();
    }

    pub(crate) fn draw_instanced_batch<T>(
        &mut self,
        data: &[T],
        vertex_array_kind: VertexArrayKind,
        textures: &BatchTextures,
        stats: &mut RendererStats,
    ) {
        for i in 0 .. textures.colors.len() {
            self.texture_resolver.bind(
                &textures.colors[i],
                TextureSampler::color(i),
                &mut self.device,
            );
        }

        // TODO: this probably isn't the best place for this.
        if let Some(ref texture) = self.dither_matrix_texture {
            self.device.bind_texture(TextureSampler::Dither, texture);
        }

        self.draw_instanced_batch_with_previously_bound_textures(data, vertex_array_kind, stats)
    }

    pub(crate) fn draw_instanced_batch_with_previously_bound_textures<T>(
        &mut self,
        data: &[T],
        vertex_array_kind: VertexArrayKind,
        stats: &mut RendererStats,
    ) {
        // If we end up with an empty draw call here, that means we have
        // probably introduced unnecessary batch breaks during frame
        // building - so we should be catching this earlier and removing
        // the batch.
        debug_assert!(!data.is_empty());

        let vao = get_vao(vertex_array_kind, &self.vaos, &self.gpu_glyph_renderer);

        self.device.bind_vao(vao);

        let batched = !self.debug_flags.contains(DebugFlags::DISABLE_BATCHING);

        if batched {
            self.device
                .update_vao_instances(vao, data, VertexUsageHint::Stream);
            self.device
                .draw_indexed_triangles_instanced_u16(6, data.len() as i32);
            self.profile_counters.draw_calls.inc();
            stats.total_draw_calls += 1;
        } else {
            for i in 0 .. data.len() {
                self.device
                    .update_vao_instances(vao, &data[i .. i + 1], VertexUsageHint::Stream);
                self.device.draw_triangles_u16(0, 6);
                self.profile_counters.draw_calls.inc();
                stats.total_draw_calls += 1;
            }
        }

        self.profile_counters.vertices.add(6 * data.len());
    }

    //TODO: make this nicer. Currently we can't accept `&mut self` because the `DrawTarget` parameter
    // needs to borrow self.texture_resolver
    fn handle_blits(
        gpu_profile: &mut GpuProfiler<GpuProfileTag>,
        device: &mut Device,
        texture_resolver: &TextureResolver,
        blits: &[BlitJob],
        render_tasks: &RenderTaskTree,
        draw_target: DrawTarget,
        content_origin: &DeviceIntPoint,
    ) {
        if blits.is_empty() {
            return;
        }

        let _timer = gpu_profile.start_timer(GPU_TAG_BLIT);

        // TODO(gw): For now, we don't bother batching these by source texture.
        //           If if ever shows up as an issue, we can easily batch them.
        for blit in blits {
            let (source, layer, source_rect) = match blit.source {
                BlitJobSource::Texture(texture_id, layer, source_rect) => {
                    // A blit from a texture into this target.
                    (texture_id, layer as usize, source_rect)
                }
                BlitJobSource::RenderTask(task_id) => {
                    // A blit from the child render task into this target.
                    // TODO(gw): Support R8 format here once we start
                    //           creating mips for alpha masks.
                    let source = &render_tasks[task_id];
                    let (source_rect, layer) = source.get_target_rect();
                    (TextureSource::PrevPassColor, layer.0, source_rect)
                }
            };
            debug_assert_eq!(source_rect.size, blit.target_rect.size);
            let texture = texture_resolver
                .resolve(&source)
                .expect("BUG: invalid source texture");
            let read_target = DrawTarget::Texture { texture, layer, with_depth: false };

            device.bind_read_target(read_target.into());
            device.blit_render_target(
                read_target.to_framebuffer_rect(source_rect),
                draw_target.to_framebuffer_rect(blit.target_rect.translate(&-content_origin.to_vector())),
                TextureFilter::Linear,
            );
        }
    }

    fn handle_scaling(
        &mut self,
        scalings: &[ScalingInstance],
        source: TextureSource,
        projection: &Transform3D<f32>,
        stats: &mut RendererStats,
    ) {
        if scalings.is_empty() {
            return
        }

        let _timer = self.gpu_profile.start_timer(GPU_TAG_SCALE);

        match source {
            TextureSource::PrevPassColor => {
                self.shaders.borrow_mut().cs_scale_rgba8.bind(&mut self.device,
                                                              &projection,
                                                              &mut self.renderer_errors);
            }
            TextureSource::PrevPassAlpha => {
                self.shaders.borrow_mut().cs_scale_a8.bind(&mut self.device,
                                                           &projection,
                                                           &mut self.renderer_errors);
            }
            _ => unreachable!(),
        }

        self.draw_instanced_batch(
            &scalings,
            VertexArrayKind::Scale,
            &BatchTextures::no_texture(),
            stats,
        );
    }

    fn draw_color_target(
        &mut self,
        draw_target: DrawTarget,
        target: &ColorRenderTarget,
        content_origin: DeviceIntPoint,
        clear_color: Option<[f32; 4]>,
        clear_depth: Option<f32>,
        render_tasks: &RenderTaskTree,
        projection: &Transform3D<f32>,
        frame_id: GpuFrameId,
        stats: &mut RendererStats,
    ) {
        self.profile_counters.color_targets.inc();
        let _gm = self.gpu_profile.start_marker("color target");

        // sanity check for the depth buffer
        if let DrawTarget::Texture { texture, .. } = draw_target {
            assert!(texture.supports_depth() >= target.needs_depth());
        }

        let framebuffer_kind = if draw_target.is_default() {
            FramebufferKind::Main
        } else {
            FramebufferKind::Other
        };

        {
            let _timer = self.gpu_profile.start_timer(GPU_TAG_SETUP_TARGET);
            self.device.bind_draw_target(draw_target);
            self.device.disable_depth();
            self.set_blend(false, framebuffer_kind);

            if clear_depth.is_some() {
                self.device.enable_depth_write();
            }

            let clear_rect = match draw_target {
                DrawTarget::Default { rect, total_size } if rect.origin == FramebufferIntPoint::zero() && rect.size == total_size => {
                    // whole screen is covered, no need for scissor
                    None
                }
                DrawTarget::Default { rect, .. } => {
                    Some(rect)
                }
                DrawTarget::Texture { .. } if self.enable_clear_scissor => {
                    // TODO(gw): Applying a scissor rect and minimal clear here
                    // is a very large performance win on the Intel and nVidia
                    // GPUs that I have tested with. It's possible it may be a
                    // performance penalty on other GPU types - we should test this
                    // and consider different code paths.
                    //
                    // Note: The above measurements were taken when render
                    // target slices were minimum 2048x2048. Now that we size
                    // them adaptively, this may be less of a win (except perhaps
                    // on a mostly-unused last slice of a large texture array).
                    Some(draw_target.to_framebuffer_rect(target.used_rect()))
                }
                DrawTarget::Texture { .. } => {
                    None
                }
            };

            self.device.clear_target(
                clear_color,
                clear_depth,
                clear_rect,
            );

            if clear_depth.is_some() {
                self.device.disable_depth_write();
            }
        }

        // Handle any blits from the texture cache to this target.
        Self::handle_blits(
            &mut self.gpu_profile, &mut self.device, &self.texture_resolver,
            &target.blits, render_tasks, draw_target, &content_origin,
        );

        // Draw any blurs for this target.
        // Blurs are rendered as a standard 2-pass
        // separable implementation.
        // TODO(gw): In the future, consider having
        //           fast path blur shaders for common
        //           blur radii with fixed weights.
        if !target.vertical_blurs.is_empty() || !target.horizontal_blurs.is_empty() {
            let _timer = self.gpu_profile.start_timer(GPU_TAG_BLUR);

            self.set_blend(false, framebuffer_kind);
            self.shaders.borrow_mut().cs_blur_rgba8
                .bind(&mut self.device, projection, &mut self.renderer_errors);

            if !target.vertical_blurs.is_empty() {
                self.draw_instanced_batch(
                    &target.vertical_blurs,
                    VertexArrayKind::Blur,
                    &BatchTextures::no_texture(),
                    stats,
                );
            }

            if !target.horizontal_blurs.is_empty() {
                self.draw_instanced_batch(
                    &target.horizontal_blurs,
                    VertexArrayKind::Blur,
                    &BatchTextures::no_texture(),
                    stats,
                );
            }
        }

        self.handle_scaling(
            &target.scalings,
            TextureSource::PrevPassColor,
            projection,
            stats,
        );

        // Small helper fn to iterate a regions list, also invoking the closure
        // if there are no regions.
        fn iterate_regions<F>(
            regions: &[DeviceIntRect],
            mut f: F,
        ) where F: FnMut(Option<DeviceIntRect>) {
            if regions.is_empty() {
                f(None)
            } else {
                for region in regions {
                    f(Some(*region))
                }
            }
        }

        for alpha_batch_container in &target.alpha_batch_containers {
            let uses_scissor = alpha_batch_container.task_scissor_rect.is_some() ||
                               !alpha_batch_container.regions.is_empty();

            if uses_scissor {
                self.device.enable_scissor();
                let scissor_rect = draw_target.build_scissor_rect(
                    alpha_batch_container.task_scissor_rect,
                    content_origin,
                );
                self.device.set_scissor_rect(scissor_rect)
            }

            if !alpha_batch_container.opaque_batches.is_empty() {
                let _gl = self.gpu_profile.start_marker("opaque batches");
                let opaque_sampler = self.gpu_profile.start_sampler(GPU_SAMPLER_TAG_OPAQUE);
                self.set_blend(false, framebuffer_kind);
                //Note: depth equality is needed for split planes
                self.device.set_depth_func(DepthFunction::LessEqual);
                self.device.enable_depth();
                self.device.enable_depth_write();

                // Draw opaque batches front-to-back for maximum
                // z-buffer efficiency!
                for batch in alpha_batch_container
                    .opaque_batches
                    .iter()
                    .rev()
                {
                    self.shaders.borrow_mut()
                        .get(&batch.key, self.debug_flags)
                        .bind(
                            &mut self.device, projection,
                            &mut self.renderer_errors,
                        );

                    let _timer = self.gpu_profile.start_timer(batch.key.kind.sampler_tag());

                    iterate_regions(
                        &alpha_batch_container.regions,
                        |region| {
                            if let Some(region) = region {
                                let scissor_rect = draw_target.build_scissor_rect(
                                    Some(region),
                                    content_origin,
                                );
                                self.device.set_scissor_rect(scissor_rect);
                            }

                            self.draw_instanced_batch(
                                &batch.instances,
                                VertexArrayKind::Primitive,
                                &batch.key.textures,
                                stats
                            );
                        }
                    );
                }

                self.device.disable_depth_write();
                self.gpu_profile.finish_sampler(opaque_sampler);
            }

            if !alpha_batch_container.alpha_batches.is_empty() {
                let _gl = self.gpu_profile.start_marker("alpha batches");
                let transparent_sampler = self.gpu_profile.start_sampler(GPU_SAMPLER_TAG_TRANSPARENT);
                self.set_blend(true, framebuffer_kind);
                let mut prev_blend_mode = BlendMode::None;

                for batch in &alpha_batch_container.alpha_batches {
                    self.shaders.borrow_mut()
                        .get(&batch.key, self.debug_flags)
                        .bind(
                            &mut self.device, projection,
                            &mut self.renderer_errors,
                        );

                    if batch.key.blend_mode != prev_blend_mode {
                        match batch.key.blend_mode {
                            _ if self.debug_flags.contains(DebugFlags::SHOW_OVERDRAW) &&
                                    framebuffer_kind == FramebufferKind::Main => {
                                self.device.set_blend_mode_show_overdraw();
                            }
                            BlendMode::None => {
                                unreachable!("bug: opaque blend in alpha pass");
                            }
                            BlendMode::Alpha => {
                                self.device.set_blend_mode_alpha();
                            }
                            BlendMode::PremultipliedAlpha => {
                                self.device.set_blend_mode_premultiplied_alpha();
                            }
                            BlendMode::PremultipliedDestOut => {
                                self.device.set_blend_mode_premultiplied_dest_out();
                            }
                            BlendMode::SubpixelDualSource => {
                                self.device.set_blend_mode_subpixel_dual_source();
                            }
                            BlendMode::SubpixelConstantTextColor(color) => {
                                self.device.set_blend_mode_subpixel_constant_text_color(color);
                            }
                            BlendMode::SubpixelWithBgColor => {
                                // Using the three pass "component alpha with font smoothing
                                // background color" rendering technique:
                                //
                                // /webrender/doc/text-rendering.md
                                //
                                self.device.set_blend_mode_subpixel_with_bg_color_pass0();
                                self.device.switch_mode(ShaderColorMode::SubpixelWithBgColorPass0 as _);
                            }
                        }
                        prev_blend_mode = batch.key.blend_mode;
                    }

                    let _timer = self.gpu_profile.start_timer(batch.key.kind.sampler_tag());

                    iterate_regions(
                        &alpha_batch_container.regions,
                        |region| {
                            if let Some(region) = region {
                                let scissor_rect = draw_target.build_scissor_rect(
                                    Some(region),
                                    content_origin,
                                );
                                self.device.set_scissor_rect(scissor_rect);
                            }

                            self.draw_instanced_batch(
                                &batch.instances,
                                VertexArrayKind::Primitive,
                                &batch.key.textures,
                                stats
                            );

                            if batch.key.blend_mode == BlendMode::SubpixelWithBgColor {
                                self.set_blend_mode_subpixel_with_bg_color_pass1(framebuffer_kind);
                                self.device.switch_mode(ShaderColorMode::SubpixelWithBgColorPass1 as _);

                                // When drawing the 2nd and 3rd passes, we know that the VAO, textures etc
                                // are all set up from the previous draw_instanced_batch call,
                                // so just issue a draw call here to avoid re-uploading the
                                // instances and re-binding textures etc.
                                self.device
                                    .draw_indexed_triangles_instanced_u16(6, batch.instances.len() as i32);

                                self.set_blend_mode_subpixel_with_bg_color_pass2(framebuffer_kind);
                                self.device.switch_mode(ShaderColorMode::SubpixelWithBgColorPass2 as _);

                                self.device
                                    .draw_indexed_triangles_instanced_u16(6, batch.instances.len() as i32);
                            }
                        }
                    );

                    if batch.key.blend_mode == BlendMode::SubpixelWithBgColor {
                        prev_blend_mode = BlendMode::None;
                    }
                }

                self.device.disable_depth();
                self.set_blend(false, framebuffer_kind);
                self.gpu_profile.finish_sampler(transparent_sampler);
            }

            if uses_scissor {
                self.device.disable_scissor();
            }

            // At the end of rendering a container, blit across any cache tiles
            // to the texture cache for use on subsequent frames.
            if !alpha_batch_container.tile_blits.is_empty() {
                let _timer = self.gpu_profile.start_timer(GPU_TAG_BLIT);

                self.device.bind_read_target(draw_target.into());

                for blit in &alpha_batch_container.tile_blits {
                    let texture = self.texture_resolver
                        .resolve(&blit.target.texture_id)
                        .expect("BUG: invalid target texture");

                    let blit_target = DrawTarget::Texture {
                        texture,
                        layer: blit.target.texture_layer as usize,
                        with_depth: false,
                    };
                    self.device.bind_draw_target(blit_target);

                    let src_rect = draw_target.to_framebuffer_rect(DeviceIntRect::new(
                        blit.src_offset - content_origin.to_vector(),
                        blit.size,
                    ));

                    let target_rect = blit.target.uv_rect.to_i32();

                    let dest_rect = blit_target.to_framebuffer_rect(DeviceIntRect::new(
                        blit.dest_offset + (target_rect.origin - content_origin),
                        blit.size,
                    ));

                    self.device.blit_render_target_invert_y(
                        src_rect,
                        dest_rect,
                    );
                }

                self.device.bind_draw_target(draw_target);
            }
        }

        // For any registered image outputs on this render target,
        // get the texture from caller and blit it.
        for output in &target.outputs {
            let handler = self.output_image_handler
                .as_mut()
                .expect("Found output image, but no handler set!");
            if let Some((texture_id, output_size)) = handler.lock(output.pipeline_id) {
                let fbo_id = match self.output_targets.entry(texture_id) {
                    Entry::Vacant(entry) => {
                        let fbo_id = self.device.create_fbo_for_external_texture(texture_id);
                        entry.insert(FrameOutput {
                            fbo_id,
                            last_access: frame_id,
                        });
                        fbo_id
                    }
                    Entry::Occupied(mut entry) => {
                        let target = entry.get_mut();
                        target.last_access = frame_id;
                        target.fbo_id
                    }
                };
                let (src_rect, _) = render_tasks[output.task_id].get_target_rect();
                self.device.bind_read_target(draw_target.into());
                self.device.bind_external_draw_target(fbo_id);
                self.device.blit_render_target_invert_y(
                    draw_target.to_framebuffer_rect(src_rect.translate(&-content_origin.to_vector())),
                    output_size.into(),
                );
                handler.unlock(output.pipeline_id);
            }
        }
    }

    /// Draw all the instances in a clip batcher list to the current target.
    fn draw_clip_batch_list(
        &mut self,
        list: &ClipBatchList,
        projection: &Transform3D<f32>,
        stats: &mut RendererStats,
    ) {
        // draw rounded cornered rectangles
        if !list.rectangles.is_empty() {
            let _gm2 = self.gpu_profile.start_marker("clip rectangles");
            self.shaders.borrow_mut().cs_clip_rectangle.bind(
                &mut self.device,
                projection,
                &mut self.renderer_errors,
            );
            self.draw_instanced_batch(
                &list.rectangles,
                VertexArrayKind::Clip,
                &BatchTextures::no_texture(),
                stats,
            );
        }
        // draw box-shadow clips
        for (mask_texture_id, items) in list.box_shadows.iter() {
            let _gm2 = self.gpu_profile.start_marker("box-shadows");
            let textures = BatchTextures {
                colors: [
                    mask_texture_id.clone(),
                    TextureSource::Invalid,
                    TextureSource::Invalid,
                ],
            };
            self.shaders.borrow_mut().cs_clip_box_shadow
                .bind(&mut self.device, projection, &mut self.renderer_errors);
            self.draw_instanced_batch(
                items,
                VertexArrayKind::Clip,
                &textures,
                stats,
            );
        }

        // draw image masks
        for (mask_texture_id, items) in list.images.iter() {
            let _gm2 = self.gpu_profile.start_marker("clip images");
            let textures = BatchTextures {
                colors: [
                    mask_texture_id.clone(),
                    TextureSource::Invalid,
                    TextureSource::Invalid,
                ],
            };
            self.shaders.borrow_mut().cs_clip_image
                .bind(&mut self.device, projection, &mut self.renderer_errors);
            self.draw_instanced_batch(
                items,
                VertexArrayKind::Clip,
                &textures,
                stats,
            );
        }
    }

    fn draw_alpha_target(
        &mut self,
        draw_target: DrawTarget,
        target: &AlphaRenderTarget,
        projection: &Transform3D<f32>,
        render_tasks: &RenderTaskTree,
        stats: &mut RendererStats,
    ) {
        self.profile_counters.alpha_targets.inc();
        let _gm = self.gpu_profile.start_marker("alpha target");
        let alpha_sampler = self.gpu_profile.start_sampler(GPU_SAMPLER_TAG_ALPHA);

        {
            let _timer = self.gpu_profile.start_timer(GPU_TAG_SETUP_TARGET);
            self.device.bind_draw_target(draw_target);
            self.device.disable_depth();
            self.device.disable_depth_write();
            self.set_blend(false, FramebufferKind::Other);

            // TODO(gw): Applying a scissor rect and minimal clear here
            // is a very large performance win on the Intel and nVidia
            // GPUs that I have tested with. It's possible it may be a
            // performance penalty on other GPU types - we should test this
            // and consider different code paths.

            let zero_color = [0.0, 0.0, 0.0, 0.0];
            for &task_id in &target.zero_clears {
                let (rect, _) = render_tasks[task_id].get_target_rect();
                self.device.clear_target(
                    Some(zero_color),
                    None,
                    Some(draw_target.to_framebuffer_rect(rect)),
                );
            }

            let one_color = [1.0, 1.0, 1.0, 1.0];
            for &task_id in &target.one_clears {
                let (rect, _) = render_tasks[task_id].get_target_rect();
                self.device.clear_target(
                    Some(one_color),
                    None,
                    Some(draw_target.to_framebuffer_rect(rect)),
                );
            }
        }

        // Draw any blurs for this target.
        // Blurs are rendered as a standard 2-pass
        // separable implementation.
        // TODO(gw): In the future, consider having
        //           fast path blur shaders for common
        //           blur radii with fixed weights.
        if !target.vertical_blurs.is_empty() || !target.horizontal_blurs.is_empty() {
            let _timer = self.gpu_profile.start_timer(GPU_TAG_BLUR);

            self.shaders.borrow_mut().cs_blur_a8
                .bind(&mut self.device, projection, &mut self.renderer_errors);

            if !target.vertical_blurs.is_empty() {
                self.draw_instanced_batch(
                    &target.vertical_blurs,
                    VertexArrayKind::Blur,
                    &BatchTextures::no_texture(),
                    stats,
                );
            }

            if !target.horizontal_blurs.is_empty() {
                self.draw_instanced_batch(
                    &target.horizontal_blurs,
                    VertexArrayKind::Blur,
                    &BatchTextures::no_texture(),
                    stats,
                );
            }
        }

        self.handle_scaling(
            &target.scalings,
            TextureSource::PrevPassAlpha,
            projection,
            stats,
        );

        // Draw the clip items into the tiled alpha mask.
        {
            let _timer = self.gpu_profile.start_timer(GPU_TAG_CACHE_CLIP);

            // TODO(gw): Consider grouping multiple clip masks per shader
            //           invocation here to reduce memory bandwith further?

            // Draw the primary clip mask - since this is the first mask
            // for the task, we can disable blending, knowing that it will
            // overwrite every pixel in the mask area.
            self.set_blend(false, FramebufferKind::Other);
            self.draw_clip_batch_list(
                &target.clip_batcher.primary_clips,
                projection,
                stats,
            );

            // switch to multiplicative blending for secondary masks, using
            // multiplicative blending to accumulate clips into the mask.
            self.set_blend(true, FramebufferKind::Other);
            self.set_blend_mode_multiply(FramebufferKind::Other);
            self.draw_clip_batch_list(
                &target.clip_batcher.secondary_clips,
                projection,
                stats,
            );
        }

        self.gpu_profile.finish_sampler(alpha_sampler);
    }

    fn draw_texture_cache_target(
        &mut self,
        texture: &CacheTextureId,
        layer: LayerIndex,
        target: &TextureCacheRenderTarget,
        render_tasks: &RenderTaskTree,
        stats: &mut RendererStats,
    ) {
        let texture_source = TextureSource::TextureCache(*texture);
        let (target_size, projection) = {
            let texture = self.texture_resolver
                .resolve(&texture_source)
                .expect("BUG: invalid target texture");
            let target_size = texture.get_dimensions();
            let projection = Transform3D::ortho(
                0.0,
                target_size.width as f32,
                0.0,
                target_size.height as f32,
                ORTHO_NEAR_PLANE,
                ORTHO_FAR_PLANE,
            );
            (target_size, projection)
        };

        self.device.disable_depth();
        self.device.disable_depth_write();

        self.set_blend(false, FramebufferKind::Other);

        // Handle any Pathfinder glyphs.
        let stencil_page = self.stencil_glyphs(&target.glyphs, &projection, &target_size, stats);

        {
            let texture = self.texture_resolver
                .resolve(&texture_source)
                .expect("BUG: invalid target texture");
            let draw_target = DrawTarget::Texture {
                texture,
                layer,
                with_depth: false,
            };
            self.device.bind_draw_target(draw_target);

            self.device.disable_depth();
            self.device.disable_depth_write();
            self.set_blend(false, FramebufferKind::Other);

            for rect in &target.clears {
                self.device.clear_target(
                    Some([0.0, 0.0, 0.0, 0.0]),
                    None,
                    Some(draw_target.to_framebuffer_rect(*rect)),
                );
            }

            // Handle any blits to this texture from child tasks.
            Self::handle_blits(
                &mut self.gpu_profile, &mut self.device, &self.texture_resolver,
                &target.blits, render_tasks, draw_target, &DeviceIntPoint::zero(),
            );
        }

        // Draw any borders for this target.
        if !target.border_segments_solid.is_empty() ||
           !target.border_segments_complex.is_empty()
        {
            let _timer = self.gpu_profile.start_timer(GPU_TAG_CACHE_BORDER);

            self.set_blend(true, FramebufferKind::Other);
            self.set_blend_mode_premultiplied_alpha(FramebufferKind::Other);

            if !target.border_segments_solid.is_empty() {
                self.shaders.borrow_mut().cs_border_solid.bind(
                    &mut self.device,
                    &projection,
                    &mut self.renderer_errors,
                );

                self.draw_instanced_batch(
                    &target.border_segments_solid,
                    VertexArrayKind::Border,
                    &BatchTextures::no_texture(),
                    stats,
                );
            }

            if !target.border_segments_complex.is_empty() {
                self.shaders.borrow_mut().cs_border_segment.bind(
                    &mut self.device,
                    &projection,
                    &mut self.renderer_errors,
                );

                self.draw_instanced_batch(
                    &target.border_segments_complex,
                    VertexArrayKind::Border,
                    &BatchTextures::no_texture(),
                    stats,
                );
            }

            self.set_blend(false, FramebufferKind::Other);
        }

        // Draw any line decorations for this target.
        if !target.line_decorations.is_empty() {
            let _timer = self.gpu_profile.start_timer(GPU_TAG_CACHE_LINE_DECORATION);

            self.set_blend(true, FramebufferKind::Other);
            self.set_blend_mode_premultiplied_alpha(FramebufferKind::Other);

            if !target.line_decorations.is_empty() {
                self.shaders.borrow_mut().cs_line_decoration.bind(
                    &mut self.device,
                    &projection,
                    &mut self.renderer_errors,
                );

                self.draw_instanced_batch(
                    &target.line_decorations,
                    VertexArrayKind::LineDecoration,
                    &BatchTextures::no_texture(),
                    stats,
                );
            }

            self.set_blend(false, FramebufferKind::Other);
        }

        // Draw any blurs for this target.
        if !target.horizontal_blurs.is_empty() {
            let _timer = self.gpu_profile.start_timer(GPU_TAG_BLUR);

            {
                let mut shaders = self.shaders.borrow_mut();
                match target.target_kind {
                    RenderTargetKind::Alpha => &mut shaders.cs_blur_a8,
                    RenderTargetKind::Color => &mut shaders.cs_blur_rgba8,
                }.bind(&mut self.device, &projection, &mut self.renderer_errors);
            }

            self.draw_instanced_batch(
                &target.horizontal_blurs,
                VertexArrayKind::Blur,
                &BatchTextures::no_texture(),
                stats,
            );
        }

        // Blit any Pathfinder glyphs to the cache texture.
        if let Some(stencil_page) = stencil_page {
            self.cover_glyphs(stencil_page, &projection, stats);
        }
    }

    #[cfg(not(feature = "pathfinder"))]
    fn stencil_glyphs(&mut self,
                      _: &[GlyphJob],
                      _: &Transform3D<f32>,
                      _: &DeviceIntSize,
                      _: &mut RendererStats)
                      -> Option<StenciledGlyphPage> {
        None
    }

    #[cfg(not(feature = "pathfinder"))]
    fn cover_glyphs(&mut self,
                    _: StenciledGlyphPage,
                    _: &Transform3D<f32>,
                    _: &mut RendererStats) {}

    fn update_deferred_resolves(&mut self, deferred_resolves: &[DeferredResolve]) -> Option<GpuCacheUpdateList> {
        // The first thing we do is run through any pending deferred
        // resolves, and use a callback to get the UV rect for this
        // custom item. Then we patch the resource_rects structure
        // here before it's uploaded to the GPU.
        if deferred_resolves.is_empty() {
            return None;
        }

        let handler = self.external_image_handler
            .as_mut()
            .expect("Found external image, but no handler set!");

        let mut list = GpuCacheUpdateList {
            frame_id: FrameId::INVALID,
            clear: false,
            height: self.gpu_cache_texture.get_height(),
            blocks: Vec::new(),
            updates: Vec::new(),
            debug_commands: Vec::new(),
        };

        for deferred_resolve in deferred_resolves {
            self.gpu_profile.place_marker("deferred resolve");
            let props = &deferred_resolve.image_properties;
            let ext_image = props
                .external_image
                .expect("BUG: Deferred resolves must be external images!");
            // Provide rendering information for NativeTexture external images.
            let image = handler.lock(ext_image.id, ext_image.channel_index, deferred_resolve.rendering);
            let texture_target = match ext_image.image_type {
                ExternalImageType::TextureHandle(target) => target,
                ExternalImageType::Buffer => {
                    panic!("not a suitable image type in update_deferred_resolves()");
                }
            };

            // In order to produce the handle, the external image handler may call into
            // the GL context and change some states.
            self.device.reset_state();

            let texture = match image.source {
                ExternalImageSource::NativeTexture(texture_id) => {
                    ExternalTexture::new(texture_id, texture_target)
                }
                ExternalImageSource::Invalid => {
                    warn!("Invalid ext-image");
                    debug!(
                        "For ext_id:{:?}, channel:{}.",
                        ext_image.id,
                        ext_image.channel_index
                    );
                    // Just use 0 as the gl handle for this failed case.
                    ExternalTexture::new(0, texture_target)
                }
                ExternalImageSource::RawData(_) => {
                    panic!("Raw external data is not expected for deferred resolves!");
                }
            };

            self.texture_resolver
                .external_images
                .insert((ext_image.id, ext_image.channel_index), texture);

            list.updates.push(GpuCacheUpdate::Copy {
                block_index: list.blocks.len(),
                block_count: BLOCKS_PER_UV_RECT,
                address: deferred_resolve.address,
            });
            list.blocks.push(image.uv.into());
            list.blocks.push([0f32; 4].into());
        }

        Some(list)
    }

    fn unlock_external_images(&mut self) {
        if !self.texture_resolver.external_images.is_empty() {
            let handler = self.external_image_handler
                .as_mut()
                .expect("Found external image, but no handler set!");

            for (ext_data, _) in self.texture_resolver.external_images.drain() {
                handler.unlock(ext_data.0, ext_data.1);
            }
        }
    }

    /// Allocates a texture to be used as the output for a rendering pass.
    ///
    /// We make an effort to reuse render targe textures across passes and
    /// across frames when the format and dimensions match. Because we use
    /// immutable storage, we can't resize textures.
    ///
    /// We could consider approaches to re-use part of a larger target, if
    /// available. However, we'd need to be careful about eviction. Currently,
    /// render targets are freed if they haven't been used in 30 frames. If we
    /// used partial targets, we'd need to track how _much_ of the target has
    /// been used in the last 30 frames, since we could otherwise end up
    /// keeping an enormous target alive indefinitely by constantly using it
    /// in situations where a much smaller target would suffice.
    fn allocate_target_texture<T: RenderTarget>(
        &mut self,
        list: &mut RenderTargetList<T>,
        counters: &mut FrameProfileCounters,
    ) -> Option<ActiveTexture> {
        if list.targets.is_empty() {
            return None
        }

        // Get a bounding rect of all the layers, and round it up to a multiple
        // of 256. This improves render target reuse when resizing the window,
        // since we don't need to create a new render target for each slightly-
        // larger frame.
        let mut bounding_rect = DeviceIntRect::zero();
        for t in list.targets.iter() {
            bounding_rect = t.used_rect().union(&bounding_rect);
        }
        debug_assert_eq!(bounding_rect.origin, DeviceIntPoint::zero());
        let dimensions = DeviceIntSize::new(
            (bounding_rect.size.width + 255) & !255,
            (bounding_rect.size.height + 255) & !255,
        );

        counters.targets_used.inc();

        // Try finding a match in the existing pool. If there's no match, we'll
        // create a new texture.
        let selector = TargetSelector {
            size: dimensions,
            num_layers: list.targets.len(),
            format: list.format,
        };
        let index = self.texture_resolver.render_target_pool
            .iter()
            .position(|texture| {
                selector == TargetSelector {
                    size: texture.get_dimensions(),
                    num_layers: texture.get_layer_count() as usize,
                    format: texture.get_format(),
                }
            });

        let rt_info = RenderTargetInfo { has_depth: list.needs_depth() };
        let texture = if let Some(idx) = index {
            let mut t = self.texture_resolver.render_target_pool.swap_remove(idx);
            self.device.reuse_render_target::<u8>(&mut t, rt_info);
            t
        } else {
            counters.targets_created.inc();
            self.device.create_texture(
                TextureTarget::Array,
                list.format,
                dimensions.width,
                dimensions.height,
                TextureFilter::Linear,
                Some(rt_info),
                list.targets.len() as _,
            )
        };

        list.check_ready(&texture);
        Some(ActiveTexture {
            texture,
            saved_index: list.saved_index.clone(),
        })
    }

    fn bind_frame_data(&mut self, frame: &mut Frame) {
        let _timer = self.gpu_profile.start_timer(GPU_TAG_SETUP_DATA);

        self.prim_header_f_texture.update(
            &mut self.device,
            &mut frame.prim_headers.headers_float,
        );
        self.device.bind_texture(
            TextureSampler::PrimitiveHeadersF,
            &self.prim_header_f_texture.texture(),
        );

        self.prim_header_i_texture.update(
            &mut self.device,
            &mut frame.prim_headers.headers_int,
        );
        self.device.bind_texture(
            TextureSampler::PrimitiveHeadersI,
            &self.prim_header_i_texture.texture(),
        );

        self.transforms_texture.update(
            &mut self.device,
            &mut frame.transform_palette,
        );
        self.device.bind_texture(
            TextureSampler::TransformPalette,
            &self.transforms_texture.texture(),
        );

        self.render_task_texture
            .update(&mut self.device, &mut frame.render_tasks.task_data);
        self.device.bind_texture(
            TextureSampler::RenderTasks,
            &self.render_task_texture.texture(),
        );

        debug_assert!(self.texture_resolver.prev_pass_alpha.is_none());
        debug_assert!(self.texture_resolver.prev_pass_color.is_none());
    }

    fn draw_tile_frame(
        &mut self,
        frame: &mut Frame,
        framebuffer_size: Option<FramebufferIntSize>,
        frame_id: GpuFrameId,
        stats: &mut RendererStats,
        fb_clear_color: Option<ColorF>,
        fb_clear_depth: Option<f32>,
    ) {
        let _gm = self.gpu_profile.start_marker("tile frame draw");

        if frame.passes.is_empty() {
            frame.has_been_rendered = true;
            return;
        }

        self.device.disable_depth_write();
        self.set_blend(false, FramebufferKind::Other);
        self.device.disable_stencil();

        self.bind_frame_data(frame);

        for (pass_index, pass) in frame.passes.iter_mut().enumerate() {
            let _gm = self.gpu_profile.start_marker(&format!("pass {}", pass_index));

            self.texture_resolver.bind(
                &TextureSource::PrevPassAlpha,
                TextureSampler::PrevPassAlpha,
                &mut self.device,
            );
            self.texture_resolver.bind(
                &TextureSource::PrevPassColor,
                TextureSampler::PrevPassColor,
                &mut self.device,
            );

            match pass.kind {
                RenderPassKind::MainFramebuffer(ref target) => {
                    if let Some(framebuffer_size) = framebuffer_size {
                        stats.color_target_count += 1;

                        let offset = frame.content_origin.to_f32();
                        let size = frame.framebuffer_rect.size.to_f32();
                        let projection = Transform3D::ortho(
                            offset.x,
                            offset.x + size.width,
                            offset.y + size.height,
                            offset.y,
                            ORTHO_NEAR_PLANE,
                            ORTHO_FAR_PLANE,
                        );

                        self.draw_color_target(
                            DrawTarget::Default {
                                rect: frame.framebuffer_rect,
                                total_size: framebuffer_size,
                            },
                            target,
                            frame.content_origin,
                            fb_clear_color.map(|color| color.to_array()),
                            fb_clear_depth,
                            &frame.render_tasks,
                            &projection,
                            frame_id,
                            stats,
                        );
                    }
                }
                RenderPassKind::OffScreen { ref mut alpha, ref mut color, ref mut texture_cache } => {
                    let alpha_tex = self.allocate_target_texture(alpha, &mut frame.profile_counters);
                    let color_tex = self.allocate_target_texture(color, &mut frame.profile_counters);

                    // If this frame has already been drawn, then any texture
                    // cache targets have already been updated and can be
                    // skipped this time.
                    if !frame.has_been_rendered {
                        for (&(texture_id, target_index), target) in texture_cache {
                            self.draw_texture_cache_target(
                                &texture_id,
                                target_index,
                                target,
                                &frame.render_tasks,
                                stats,
                            );
                        }
                    }

                    for (target_index, target) in alpha.targets.iter().enumerate() {
                        stats.alpha_target_count += 1;
                        let draw_target = DrawTarget::Texture {
                            texture: &alpha_tex.as_ref().unwrap().texture,
                            layer: target_index,
                            with_depth: false,
                        };

                        let projection = Transform3D::ortho(
                            0.0,
                            draw_target.dimensions().width as f32,
                            0.0,
                            draw_target.dimensions().height as f32,
                            ORTHO_NEAR_PLANE,
                            ORTHO_FAR_PLANE,
                        );

                        self.draw_alpha_target(
                            draw_target,
                            target,
                            &projection,
                            &frame.render_tasks,
                            stats,
                        );
                    }

                    for (target_index, target) in color.targets.iter().enumerate() {
                        stats.color_target_count += 1;
                        let draw_target = DrawTarget::Texture {
                            texture: &color_tex.as_ref().unwrap().texture,
                            layer: target_index,
                            with_depth: target.needs_depth(),
                        };

                        let projection = Transform3D::ortho(
                            0.0,
                            draw_target.dimensions().width as f32,
                            0.0,
                            draw_target.dimensions().height as f32,
                            ORTHO_NEAR_PLANE,
                            ORTHO_FAR_PLANE,
                        );

                        let clear_depth = if target.needs_depth() {
                            Some(1.0)
                        } else {
                            None
                        };

                        self.draw_color_target(
                            draw_target,
                            target,
                            frame.content_origin,
                            Some([0.0, 0.0, 0.0, 0.0]),
                            clear_depth,
                            &frame.render_tasks,
                            &projection,
                            frame_id,
                            stats,
                        );
                    }

                    // Only end the pass here and invalidate previous textures for
                    // off-screen targets. Deferring return of the inputs to the
                    // frame buffer until the implicit end_pass in end_frame allows
                    // debug draw overlays to be added without triggering a copy
                    // resolve stage in mobile / tiled GPUs.
                    self.texture_resolver.end_pass(
                        &mut self.device,
                        alpha_tex,
                        color_tex,
                    );
                }
            }
        }

        if let Some(framebuffer_size) = framebuffer_size {
            self.draw_frame_debug_items(&frame.debug_items);
            self.draw_render_target_debug(framebuffer_size);
            self.draw_texture_cache_debug(framebuffer_size);
            self.draw_gpu_cache_debug(framebuffer_size);
            self.draw_zoom_debug(framebuffer_size);
        }
        self.draw_epoch_debug();

        // Garbage collect any frame outputs that weren't used this frame.
        let device = &mut self.device;
        self.output_targets
            .retain(|_, target| if target.last_access != frame_id {
                device.delete_fbo(target.fbo_id);
                false
            } else {
                true
            });

        frame.has_been_rendered = true;
    }

    pub fn debug_renderer<'b>(&'b mut self) -> Option<&'b mut DebugRenderer> {
        self.debug.get_mut(&mut self.device)
    }

    pub fn get_debug_flags(&self) -> DebugFlags {
        self.debug_flags
    }

    pub fn set_debug_flags(&mut self, flags: DebugFlags) {
        if let Some(enabled) = flag_changed(self.debug_flags, flags, DebugFlags::GPU_TIME_QUERIES) {
            if enabled {
                self.gpu_profile.enable_timers();
            } else {
                self.gpu_profile.disable_timers();
            }
        }
        if let Some(enabled) = flag_changed(self.debug_flags, flags, DebugFlags::GPU_SAMPLE_QUERIES) {
            if enabled {
                self.gpu_profile.enable_samplers();
            } else {
                self.gpu_profile.disable_samplers();
            }
        }

        self.debug_flags = flags;
    }

    pub fn save_cpu_profile(&self, filename: &str) {
        write_profile(filename);
    }

    fn draw_frame_debug_items(&mut self, items: &[DebugItem]) {
        if items.is_empty() {
            return;
        }

        let debug_renderer = match self.debug.get_mut(&mut self.device) {
            Some(render) => render,
            None => return,
        };

        for item in items {
            match item {
                DebugItem::Rect { rect, color } => {
                    let inner_color = color.scale_alpha(0.5).into();
                    let outer_color = (*color).into();

                    debug_renderer.add_quad(
                        rect.origin.x,
                        rect.origin.y,
                        rect.origin.x + rect.size.width,
                        rect.origin.y + rect.size.height,
                        inner_color,
                        inner_color,
                    );

                    debug_renderer.add_rect(
                        &rect.to_i32(),
                        outer_color,
                    );
                }
                DebugItem::Text { ref msg, position, color } => {
                    debug_renderer.add_text(
                        position.x,
                        position.y,
                        msg,
                        (*color).into(),
                        None,
                    );
                }
            }
        }
    }

    fn draw_render_target_debug(&mut self, framebuffer_size: FramebufferIntSize) {
        if !self.debug_flags.contains(DebugFlags::RENDER_TARGET_DBG) {
            return;
        }

        let debug_renderer = match self.debug.get_mut(&mut self.device) {
            Some(render) => render,
            None => return,
        };

        let textures =
            self.texture_resolver.render_target_pool.iter().collect::<Vec<&Texture>>();

        Self::do_debug_blit(
            &mut self.device,
            debug_renderer,
            textures,
            framebuffer_size,
            0,
            &|_| [0.0, 1.0, 0.0, 1.0], // Use green for all RTs.
        );
    }

    fn draw_zoom_debug(
        &mut self,
        framebuffer_size: FramebufferIntSize,
    ) {
        if !self.debug_flags.contains(DebugFlags::ZOOM_DBG) {
            return;
        }

        let debug_renderer = match self.debug.get_mut(&mut self.device) {
            Some(render) => render,
            None => return,
        };

        let source_size = DeviceIntSize::new(64, 64);
        let target_size = DeviceIntSize::new(1024, 1024);

        let source_origin = DeviceIntPoint::new(
            (self.cursor_position.x - source_size.width / 2)
                .min(framebuffer_size.width - source_size.width)
                .max(0),
            (self.cursor_position.y - source_size.height / 2)
                .min(framebuffer_size.height - source_size.height)
                .max(0),
        );

        let source_rect = DeviceIntRect::new(
            source_origin,
            source_size,
        );

        let target_rect = DeviceIntRect::new(
            DeviceIntPoint::new(
                framebuffer_size.width - target_size.width - 64,
                framebuffer_size.height - target_size.height - 64,
            ),
            target_size,
        );

        let texture_rect = FramebufferIntRect::new(
            FramebufferIntPoint::zero(),
            FramebufferIntSize::from_untyped(&source_rect.size.to_untyped()),
        );

        debug_renderer.add_rect(
            &target_rect.inflate(1, 1),
            debug_colors::RED.into(),
        );

        if self.zoom_debug_texture.is_none() {
            let texture = self.device.create_texture(
                TextureTarget::Default,
                ImageFormat::BGRA8,
                source_rect.size.width,
                source_rect.size.height,
                TextureFilter::Nearest,
                Some(RenderTargetInfo { has_depth: false }),
                1,
            );

            self.zoom_debug_texture = Some(texture);
        }

        // Copy frame buffer into the zoom texture
        let read_target = DrawTarget::new_default(framebuffer_size);
        self.device.bind_read_target(read_target.into());
        self.device.bind_draw_target(DrawTarget::Texture {
            texture: self.zoom_debug_texture.as_ref().unwrap(),
            layer: 0,
            with_depth: false,
        });
        self.device.blit_render_target(
            read_target.to_framebuffer_rect(source_rect),
            texture_rect,
            TextureFilter::Nearest,
        );

        // Draw the zoom texture back to the framebuffer
        self.device.bind_read_target(ReadTarget::Texture {
            texture: self.zoom_debug_texture.as_ref().unwrap(),
            layer: 0,
        });
        self.device.bind_draw_target(read_target);
        self.device.blit_render_target(
            texture_rect,
            read_target.to_framebuffer_rect(target_rect),
            TextureFilter::Nearest,
        );
    }

    fn draw_texture_cache_debug(&mut self, framebuffer_size: FramebufferIntSize) {
        if !self.debug_flags.contains(DebugFlags::TEXTURE_CACHE_DBG) {
            return;
        }

        let debug_renderer = match self.debug.get_mut(&mut self.device) {
            Some(render) => render,
            None => return,
        };

        let textures =
            self.texture_resolver.texture_cache_map.values().collect::<Vec<&Texture>>();

        fn select_color(texture: &Texture) -> [f32; 4] {
            if texture.flags().contains(TextureFlags::IS_SHARED_TEXTURE_CACHE) {
                [1.0, 0.5, 0.0, 1.0] // Orange for shared.
            } else {
                [1.0, 0.0, 1.0, 1.0] // Fuchsia for standalone.
            }
        }

        Self::do_debug_blit(
            &mut self.device,
            debug_renderer,
            textures,
            framebuffer_size,
            if self.debug_flags.contains(DebugFlags::RENDER_TARGET_DBG) { 544 } else { 0 },
            &select_color,
        );
    }

    fn do_debug_blit(
        device: &mut Device,
        debug_renderer: &mut DebugRenderer,
        mut textures: Vec<&Texture>,
        framebuffer_size: FramebufferIntSize,
        bottom: i32,
        select_color: &Fn(&Texture) -> [f32; 4],
    ) {
        let mut spacing = 16;
        let mut size = 512;

        let fb_width = framebuffer_size.width as i32;
        let fb_height = framebuffer_size.height as i32;
        let num_layers: i32 = textures.iter()
            .map(|texture| texture.get_layer_count())
            .sum();

        if num_layers * (size + spacing) > fb_width {
            let factor = fb_width as f32 / (num_layers * (size + spacing)) as f32;
            size = (size as f32 * factor) as i32;
            spacing = (spacing as f32 * factor) as i32;
        }

        // Sort the display by layer size (in bytes), so that left-to-right is
        // largest-to-smallest.
        //
        // Note that the vec here is in increasing order, because the elements
        // get drawn right-to-left.
        textures.sort_by_key(|t| t.layer_size_in_bytes());

        let mut i = 0;
        for texture in textures.iter() {
            let y = spacing + bottom;
            let dimensions = texture.get_dimensions();
            let src_rect = FramebufferIntRect::new(
                FramebufferIntPoint::zero(),
                FramebufferIntSize::new(dimensions.width as i32, dimensions.height as i32),
            );

            let layer_count = texture.get_layer_count() as usize;
            for layer in 0 .. layer_count {
                device.bind_read_target(ReadTarget::Texture { texture, layer});

                let x = fb_width - (spacing + size) * (i as i32 + 1);

                // If we have more targets than fit on one row in screen, just early exit.
                if x > fb_width {
                    return;
                }

                //TODO: properly use FramebufferPixel coordinates

                // Draw the info tag.
                let text_margin = 1;
                let text_height = 14; // Visually aproximated.
                let tag_height = text_height + text_margin * 2;
                let tag_rect = rect(x, y, size, tag_height);
                let tag_color = select_color(texture);
                device.clear_target(
                    Some(tag_color),
                    None,
                    Some(FramebufferIntRect::from_untyped(&tag_rect.to_untyped())),
                );

                // Draw the dimensions onto the tag.
                let dim = texture.get_dimensions();
                let mut text_rect = tag_rect;
                text_rect.origin.y =
                    fb_height - text_rect.origin.y - text_rect.size.height; // Top-relative.
                debug_renderer.add_text(
                    (x + text_margin) as f32,
                    (fb_height - y - text_margin) as f32, // Top-relative.
                    &format!("{}x{}", dim.width, dim.height),
                    ColorU::new(0, 0, 0, 255),
                    Some(text_rect.to_f32())
                );

                // Blit the contents of the layer. We need to invert Y because
                // we're blitting from a texture to the main framebuffer, which
                // use different conventions.
                let dest_rect = rect(x, y + tag_height, size, size);
                device.blit_render_target_invert_y(
                    src_rect,
                    FramebufferIntRect::from_untyped(&dest_rect),
                );
                i += 1;
            }
        }
    }

    fn draw_epoch_debug(&mut self) {
        if !self.debug_flags.contains(DebugFlags::EPOCHS) {
            return;
        }

        let debug_renderer = match self.debug.get_mut(&mut self.device) {
            Some(render) => render,
            None => return,
        };

        let dy = debug_renderer.line_height();
        let x0: f32 = 30.0;
        let y0: f32 = 30.0;
        let mut y = y0;
        let mut text_width = 0.0;
        for (pipeline, epoch) in  &self.pipeline_info.epochs {
            y += dy;
            let w = debug_renderer.add_text(
                x0, y,
                &format!("{:?}: {:?}", pipeline, epoch),
                ColorU::new(255, 255, 0, 255),
                None,
            ).size.width;
            text_width = f32::max(text_width, w);
        }

        let margin = 10.0;
        debug_renderer.add_quad(
            &x0 - margin,
            y0 - margin,
            x0 + text_width + margin,
            y + margin,
            ColorU::new(25, 25, 25, 200),
            ColorU::new(51, 51, 51, 200),
        );
    }

    fn draw_gpu_cache_debug(&mut self, framebuffer_size: FramebufferIntSize) {
        if !self.debug_flags.contains(DebugFlags::GPU_CACHE_DBG) {
            return;
        }

        let debug_renderer = match self.debug.get_mut(&mut self.device) {
            Some(render) => render,
            None => return,
        };

        let (x_off, y_off) = (30f32, 30f32);
        let height = self.gpu_cache_texture.texture
            .as_ref().map_or(0, |t| t.get_dimensions().height)
            .min(framebuffer_size.height - (y_off as i32) * 2) as usize;
        debug_renderer.add_quad(
            x_off,
            y_off,
            x_off + MAX_VERTEX_TEXTURE_WIDTH as f32,
            y_off + height as f32,
            ColorU::new(80, 80, 80, 80),
            ColorU::new(80, 80, 80, 80),
        );

        let upper = self.gpu_cache_debug_chunks.len().min(height);
        for chunk in self.gpu_cache_debug_chunks[0..upper].iter().flatten() {
            let color = ColorU::new(250, 0, 0, 200);
            debug_renderer.add_quad(
                x_off + chunk.address.u as f32,
                y_off + chunk.address.v as f32,
                x_off + chunk.address.u as f32 + chunk.size as f32,
                y_off + chunk.address.v as f32 + 1.0,
                color,
                color,
            );
        }
    }

    /// Pass-through to `Device::read_pixels_into`, used by Gecko's WR bindings.
    pub fn read_pixels_into(&mut self, rect: FramebufferIntRect, format: ReadPixelsFormat, output: &mut [u8]) {
        self.device.read_pixels_into(rect, format, output);
    }

    pub fn read_pixels_rgba8(&mut self, rect: FramebufferIntRect) -> Vec<u8> {
        let mut pixels = vec![0; (rect.size.width * rect.size.height * 4) as usize];
        self.device.read_pixels_into(rect, ReadPixelsFormat::Rgba8, &mut pixels);
        pixels
    }

    pub fn read_gpu_cache(&mut self) -> (FramebufferIntSize, Vec<u8>) {
        let texture = self.gpu_cache_texture.texture.as_ref().unwrap();
        let size = FramebufferIntSize::from_untyped(&texture.get_dimensions().to_untyped());
        let mut texels = vec![0; (size.width * size.height * 16) as usize];
        self.device.begin_frame();
        self.device.bind_read_target(ReadTarget::Texture { texture, layer: 0 });
        self.device.read_pixels_into(
            size.into(),
            ReadPixelsFormat::Standard(ImageFormat::RGBAF32),
            &mut texels,
        );
        self.device.reset_read_target();
        self.device.end_frame();
        (size, texels)
    }

    // De-initialize the Renderer safely, assuming the GL is still alive and active.
    pub fn deinit(mut self) {
        //Note: this is a fake frame, only needed because texture deletion is require to happen inside a frame
        self.device.begin_frame();
        self.gpu_cache_texture.deinit(&mut self.device);
        if let Some(dither_matrix_texture) = self.dither_matrix_texture {
            self.device.delete_texture(dither_matrix_texture);
        }
        if let Some(zoom_debug_texture) = self.zoom_debug_texture {
            self.device.delete_texture(zoom_debug_texture);
        }
        self.transforms_texture.deinit(&mut self.device);
        self.prim_header_f_texture.deinit(&mut self.device);
        self.prim_header_i_texture.deinit(&mut self.device);
        self.render_task_texture.deinit(&mut self.device);
        self.device.delete_pbo(self.texture_cache_upload_pbo);
        self.texture_resolver.deinit(&mut self.device);
        self.device.delete_vao(self.vaos.prim_vao);
        self.device.delete_vao(self.vaos.clip_vao);
        self.device.delete_vao(self.vaos.blur_vao);
        self.device.delete_vao(self.vaos.line_vao);
        self.device.delete_vao(self.vaos.border_vao);
        self.device.delete_vao(self.vaos.scale_vao);

        self.debug.deinit(&mut self.device);

        for (_, target) in self.output_targets {
            self.device.delete_fbo(target.fbo_id);
        }
        if let Ok(shaders) = Rc::try_unwrap(self.shaders) {
            shaders.into_inner().deinit(&mut self.device);
        }
        #[cfg(feature = "capture")]
        self.device.delete_fbo(self.read_fbo);
        #[cfg(feature = "replay")]
        for (_, ext) in self.owned_external_images {
            self.device.delete_external_texture(ext);
        }
        self.device.end_frame();
    }

    fn size_of<T>(&self, ptr: *const T) -> usize {
        let op = self.size_of_ops.as_ref().unwrap().size_of_op;
        unsafe { op(ptr as *const c_void) }
    }

    /// Collects a memory report.
    pub fn report_memory(&self) -> MemoryReport {
        let mut report = MemoryReport::default();

        // GPU cache CPU memory.
        if let GpuCacheBus::PixelBuffer{ref rows, ..} = self.gpu_cache_texture.bus {
            for row in rows.iter() {
                report.gpu_cache_cpu_mirror += self.size_of(&*row.cpu_blocks as *const _);
            }
        }

        // GPU cache GPU memory.
        report.gpu_cache_textures +=
            self.gpu_cache_texture.texture.as_ref().map_or(0, |t| t.size_in_bytes());

        // Render task CPU memory.
        for (_id, doc) in &self.active_documents {
            report.render_tasks += self.size_of(doc.frame.render_tasks.tasks.as_ptr());
            report.render_tasks += self.size_of(doc.frame.render_tasks.task_data.as_ptr());
        }

        // Vertex data GPU memory.
        report.vertex_data_textures += self.prim_header_f_texture.size_in_bytes();
        report.vertex_data_textures += self.prim_header_i_texture.size_in_bytes();
        report.vertex_data_textures += self.transforms_texture.size_in_bytes();
        report.vertex_data_textures += self.render_task_texture.size_in_bytes();

        // Texture cache and render target GPU memory.
        report += self.texture_resolver.report_memory();

        // Textures held internally within the device layer.
        report += self.device.report_memory();

        report
    }

    // Sets the blend mode. Blend is unconditionally set if the "show overdraw" debugging mode is
    // enabled.
    fn set_blend(&self, mut blend: bool, framebuffer_kind: FramebufferKind) {
        if framebuffer_kind == FramebufferKind::Main &&
                self.debug_flags.contains(DebugFlags::SHOW_OVERDRAW) {
            blend = true
        }
        self.device.set_blend(blend)
    }

    fn set_blend_mode_multiply(&self, framebuffer_kind: FramebufferKind) {
        if framebuffer_kind == FramebufferKind::Main &&
                self.debug_flags.contains(DebugFlags::SHOW_OVERDRAW) {
            self.device.set_blend_mode_show_overdraw();
        } else {
            self.device.set_blend_mode_multiply();
        }
    }

    fn set_blend_mode_premultiplied_alpha(&self, framebuffer_kind: FramebufferKind) {
        if framebuffer_kind == FramebufferKind::Main &&
                self.debug_flags.contains(DebugFlags::SHOW_OVERDRAW) {
            self.device.set_blend_mode_show_overdraw();
        } else {
            self.device.set_blend_mode_premultiplied_alpha();
        }
    }

    fn set_blend_mode_subpixel_with_bg_color_pass1(&self, framebuffer_kind: FramebufferKind) {
        if framebuffer_kind == FramebufferKind::Main &&
                self.debug_flags.contains(DebugFlags::SHOW_OVERDRAW) {
            self.device.set_blend_mode_show_overdraw();
        } else {
            self.device.set_blend_mode_subpixel_with_bg_color_pass1();
        }
    }

    fn set_blend_mode_subpixel_with_bg_color_pass2(&self, framebuffer_kind: FramebufferKind) {
        if framebuffer_kind == FramebufferKind::Main &&
                self.debug_flags.contains(DebugFlags::SHOW_OVERDRAW) {
            self.device.set_blend_mode_show_overdraw();
        } else {
            self.device.set_blend_mode_subpixel_with_bg_color_pass2();
        }
    }

    /// Clears all the layers of a texture with a given color.
    fn clear_texture(&mut self, texture: &Texture, color: [f32; 4]) {
        for i in 0..texture.get_layer_count() {
            self.device.bind_draw_target(DrawTarget::Texture {
                texture: &texture,
                layer: i as usize,
                with_depth: false,
            });
            self.device.clear_target(Some(color), None, None);
        }
    }
}

pub enum ExternalImageSource<'a> {
    RawData(&'a [u8]),  // raw buffers.
    NativeTexture(u32), // It's a gl::GLuint texture handle
    Invalid,
}

/// The data that an external client should provide about
/// an external image. The timestamp is used to test if
/// the renderer should upload new texture data this
/// frame. For instance, if providing video frames, the
/// application could call wr.render() whenever a new
/// video frame is ready. If the callback increments
/// the returned timestamp for a given image, the renderer
/// will know to re-upload the image data to the GPU.
/// Note that the UV coords are supplied in texel-space!
pub struct ExternalImage<'a> {
    pub uv: TexelRect,
    pub source: ExternalImageSource<'a>,
}

/// The interfaces that an application can implement to support providing
/// external image buffers.
/// When the the application passes an external image to WR, it should kepp that
/// external image life time. People could check the epoch id in RenderNotifier
/// at the client side to make sure that the external image is not used by WR.
/// Then, do the clean up for that external image.
pub trait ExternalImageHandler {
    /// Lock the external image. Then, WR could start to read the image content.
    /// The WR client should not change the image content until the unlock()
    /// call. Provide ImageRendering for NativeTexture external images.
    fn lock(&mut self, key: ExternalImageId, channel_index: u8, rendering: ImageRendering) -> ExternalImage;
    /// Unlock the external image. The WR should not read the image content
    /// after this call.
    fn unlock(&mut self, key: ExternalImageId, channel_index: u8);
}

/// Allows callers to receive a texture with the contents of a specific
/// pipeline copied to it. Lock should return the native texture handle
/// and the size of the texture. Unlock will only be called if the lock()
/// call succeeds, when WR has issued the GL commands to copy the output
/// to the texture handle.
pub trait OutputImageHandler {
    fn lock(&mut self, pipeline_id: PipelineId) -> Option<(u32, FramebufferIntSize)>;
    fn unlock(&mut self, pipeline_id: PipelineId);
}

pub trait ThreadListener {
    fn thread_started(&self, thread_name: &str);
    fn thread_stopped(&self, thread_name: &str);
}

/// Allows callers to hook in at certain points of the async scene build. These
/// functions are all called from the scene builder thread.
pub trait SceneBuilderHooks {
    /// This is called exactly once, when the scene builder thread is started
    /// and before it processes anything.
    fn register(&self);
    /// This is called before each scene build starts.
    fn pre_scene_build(&self);
    /// This is called before each scene swap occurs.
    fn pre_scene_swap(&self, scenebuild_time: u64);
    /// This is called after each scene swap occurs. The PipelineInfo contains
    /// the updated epochs and pipelines removed in the new scene compared to
    /// the old scene.
    fn post_scene_swap(&self, info: PipelineInfo, sceneswap_time: u64);
    /// This is called after a resource update operation on the scene builder
    /// thread, in the case where resource updates were applied without a scene
    /// build.
    fn post_resource_update(&self);
    /// This is called after a scene build completes without any changes being
    /// made. We guarantee that each pre_scene_build call will be matched with
    /// exactly one of post_scene_swap, post_resource_update or
    /// post_empty_scene_build.
    fn post_empty_scene_build(&self);
    /// This is a generic callback which provides an opportunity to run code
    /// on the scene builder thread. This is called as part of the main message
    /// loop of the scene builder thread, but outside of any specific message
    /// handler.
    fn poke(&self);
    /// This is called exactly once, when the scene builder thread is about to
    /// terminate.
    fn deregister(&self);
}

/// Allows callers to hook into the main render_backend loop and provide
/// additional frame ops for generate_frame transactions. These functions
/// are all called from the render backend thread.
pub trait AsyncPropertySampler {
    /// This is called exactly once, when the render backend thread is started
    /// and before it processes anything.
    fn register(&self);
    /// This is called for each transaction with the generate_frame flag set
    /// (i.e. that will trigger a render). The list of frame messages returned
    /// are processed as though they were part of the original transaction.
    fn sample(&self) -> Vec<FrameMsg>;
    /// This is called exactly once, when the render backend thread is about to
    /// terminate.
    fn deregister(&self);
}

/// Flags that control how shaders are pre-cached, if at all.
bitflags! {
    #[derive(Default)]
    pub struct ShaderPrecacheFlags: u32 {
        /// Needed for const initialization
        const EMPTY                 = 0;

        /// Only start async compile
        const ASYNC_COMPILE         = 1 << 2;

        /// Do a full compile/link during startup
        const FULL_COMPILE          = 1 << 3;
    }
}

pub struct RendererOptions {
    pub device_pixel_ratio: f32,
    pub resource_override_path: Option<PathBuf>,
    pub enable_aa: bool,
    pub enable_dithering: bool,
    pub max_recorded_profiles: usize,
    pub precache_flags: ShaderPrecacheFlags,
    pub renderer_kind: RendererKind,
    pub enable_subpixel_aa: bool,
    pub clear_color: Option<ColorF>,
    pub enable_clear_scissor: bool,
    pub max_texture_size: Option<i32>,
    pub scatter_gpu_cache_updates: bool,
    pub upload_method: UploadMethod,
    pub workers: Option<Arc<ThreadPool>>,
    pub blob_image_handler: Option<Box<BlobImageHandler>>,
    pub recorder: Option<Box<ApiRecordingReceiver>>,
    pub thread_listener: Option<Box<ThreadListener + Send + Sync>>,
    pub size_of_op: Option<VoidPtrToSizeFn>,
    pub enclosing_size_of_op: Option<VoidPtrToSizeFn>,
    pub cached_programs: Option<Rc<ProgramCache>>,
    pub debug_flags: DebugFlags,
    pub renderer_id: Option<u64>,
    pub disable_dual_source_blending: bool,
    pub scene_builder_hooks: Option<Box<SceneBuilderHooks + Send>>,
    pub sampler: Option<Box<AsyncPropertySampler + Send>>,
    pub chase_primitive: ChasePrimitive,
    pub support_low_priority_transactions: bool,
    pub namespace_alloc_by_client: bool,
    pub enable_picture_caching: bool,
    pub testing: bool,
    /// Set to true if this GPU supports hardware fast clears as a performance
    /// optimization. Likely requires benchmarking on various GPUs to see if
    /// it is a performance win. The default is false, which tends to be best
    /// performance on lower end / integrated GPUs.
    pub gpu_supports_fast_clears: bool,
}

impl Default for RendererOptions {
    fn default() -> Self {
        RendererOptions {
            device_pixel_ratio: 1.0,
            resource_override_path: None,
            enable_aa: true,
            enable_dithering: true,
            debug_flags: DebugFlags::empty(),
            max_recorded_profiles: 0,
            precache_flags: ShaderPrecacheFlags::empty(),
            renderer_kind: RendererKind::Native,
            enable_subpixel_aa: false,
            clear_color: Some(ColorF::new(1.0, 1.0, 1.0, 1.0)),
            enable_clear_scissor: true,
            max_texture_size: None,
            // Scattered GPU cache updates haven't met a test that would show their superiority yet.
            scatter_gpu_cache_updates: false,
            // This is best as `Immediate` on Angle, or `Pixelbuffer(Dynamic)` on GL,
            // but we are unable to make this decision here, so picking the reasonable medium.
            upload_method: UploadMethod::PixelBuffer(VertexUsageHint::Stream),
            workers: None,
            blob_image_handler: None,
            recorder: None,
            thread_listener: None,
            size_of_op: None,
            enclosing_size_of_op: None,
            renderer_id: None,
            cached_programs: None,
            disable_dual_source_blending: false,
            scene_builder_hooks: None,
            sampler: None,
            chase_primitive: ChasePrimitive::Nothing,
            support_low_priority_transactions: false,
            namespace_alloc_by_client: false,
            enable_picture_caching: false,
            testing: false,
            gpu_supports_fast_clears: false,
        }
    }
}

#[cfg(not(feature = "debugger"))]
pub struct DebugServer;

#[cfg(not(feature = "debugger"))]
impl DebugServer {
    pub fn new(_: MsgSender<ApiMsg>) -> Self {
        DebugServer
    }

    pub fn send(&mut self, _: String) {}
}

/// Some basic statistics about the rendered scene, used in Gecko, as
/// well as in wrench reftests to ensure that tests are batching and/or
/// allocating on render targets as we expect them to.
#[repr(C)]
#[derive(Debug, Default)]
pub struct RendererStats {
    pub total_draw_calls: usize,
    pub alpha_target_count: usize,
    pub color_target_count: usize,
    pub texture_upload_kb: usize,
    pub resource_upload_time: u64,
    pub gpu_cache_upload_time: u64,
}

/// Return type from render(), which contains some repr(C) statistics as well as
/// some non-repr(C) data.
#[derive(Debug, Default)]
pub struct RenderResults {
    pub stats: RendererStats,
    pub recorded_dirty_regions: Vec<RecordedDirtyRegion>,
}

#[cfg(any(feature = "capture", feature = "replay"))]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
struct PlainTexture {
    data: String,
    size: (DeviceIntSize, i32),
    format: ImageFormat,
    filter: TextureFilter,
}


#[cfg(any(feature = "capture", feature = "replay"))]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
struct PlainRenderer {
    framebuffer_size: Option<FramebufferIntSize>,
    gpu_cache: PlainTexture,
    gpu_cache_frame_id: FrameId,
    textures: FastHashMap<CacheTextureId, PlainTexture>,
    external_images: Vec<ExternalCaptureImage>
}

#[cfg(feature = "replay")]
enum CapturedExternalImageData {
    NativeTexture(gl::GLuint),
    Buffer(Arc<Vec<u8>>),
}

#[cfg(feature = "replay")]
struct DummyExternalImageHandler {
    data: FastHashMap<(ExternalImageId, u8), (CapturedExternalImageData, TexelRect)>,
}

#[cfg(feature = "replay")]
impl ExternalImageHandler for DummyExternalImageHandler {
    fn lock(&mut self, key: ExternalImageId, channel_index: u8, _rendering: ImageRendering) -> ExternalImage {
        let (ref captured_data, ref uv) = self.data[&(key, channel_index)];
        ExternalImage {
            uv: *uv,
            source: match *captured_data {
                CapturedExternalImageData::NativeTexture(tid) => ExternalImageSource::NativeTexture(tid),
                CapturedExternalImageData::Buffer(ref arc) => ExternalImageSource::RawData(&*arc),
            }
        }
    }
    fn unlock(&mut self, _key: ExternalImageId, _channel_index: u8) {}
}

#[cfg(feature = "replay")]
impl OutputImageHandler for () {
    fn lock(&mut self, _: PipelineId) -> Option<(u32, FramebufferIntSize)> {
        None
    }
    fn unlock(&mut self, _: PipelineId) {
        unreachable!()
    }
}

#[derive(Default)]
pub struct PipelineInfo {
    pub epochs: FastHashMap<PipelineId, Epoch>,
    pub removed_pipelines: Vec<PipelineId>,
}

impl Renderer {
    #[cfg(feature = "capture")]
    fn save_texture(
        texture: &Texture, name: &str, root: &PathBuf, device: &mut Device
    ) -> PlainTexture {
        use std::fs;
        use std::io::Write;

        let short_path = format!("textures/{}.raw", name);

        let bytes_per_pixel = texture.get_format().bytes_per_pixel();
        let read_format = ReadPixelsFormat::Standard(texture.get_format());
        let rect_size = texture.get_dimensions();

        let mut file = fs::File::create(root.join(&short_path))
            .expect(&format!("Unable to create {}", short_path));
        let bytes_per_layer = (rect_size.width * rect_size.height * bytes_per_pixel) as usize;
        let mut data = vec![0; bytes_per_layer];

        //TODO: instead of reading from an FBO with `read_pixels*`, we could
        // read from textures directly with `get_tex_image*`.

        for layer_id in 0 .. texture.get_layer_count() {
            let rect = FramebufferIntSize::from_untyped(&rect_size.to_untyped()).into();

            device.attach_read_texture(texture, layer_id);
            #[cfg(feature = "png")]
            {
                let mut png_data;
                let (data_ref, format) = match texture.get_format() {
                    ImageFormat::RGBAF32 => {
                        png_data = vec![0; (rect_size.width * rect_size.height * 4) as usize];
                        device.read_pixels_into(rect, ReadPixelsFormat::Rgba8, &mut png_data);
                        (&png_data, ReadPixelsFormat::Rgba8)
                    }
                    fm => (&data, ReadPixelsFormat::Standard(fm)),
                };
                CaptureConfig::save_png(
                    root.join(format!("textures/{}-{}.png", name, layer_id)),
                    rect_size, format,
                    data_ref,
                );
            }
            device.read_pixels_into(rect, read_format, &mut data);
            file.write_all(&data)
                .unwrap();
        }

        PlainTexture {
            data: short_path,
            size: (rect_size, texture.get_layer_count()),
            format: texture.get_format(),
            filter: texture.get_filter(),
        }
    }

    #[cfg(feature = "replay")]
    fn load_texture(
        target: TextureTarget,
        plain: &PlainTexture,
        rt_info: Option<RenderTargetInfo>,
        root: &PathBuf,
        device: &mut Device
    ) -> (Texture, Vec<u8>)
    {
        use std::fs::File;
        use std::io::Read;

        let mut texels = Vec::new();
        File::open(root.join(&plain.data))
            .expect(&format!("Unable to open texture at {}", plain.data))
            .read_to_end(&mut texels)
            .unwrap();

        let texture = device.create_texture(
            target,
            plain.format,
            plain.size.0.width,
            plain.size.0.height,
            plain.filter,
            rt_info,
            plain.size.1,
        );
        device.upload_texture_immediate(&texture, &texels);

        (texture, texels)
    }

    #[cfg(feature = "capture")]
    fn save_capture(
        &mut self,
        config: CaptureConfig,
        deferred_images: Vec<ExternalCaptureImage>,
    ) {
        use std::fs;
        use std::io::Write;
        use api::{CaptureBits, ExternalImageData};

        self.device.begin_frame();
        let _gm = self.gpu_profile.start_marker("read GPU data");
        self.device.bind_read_target_impl(self.read_fbo);

        if !deferred_images.is_empty() {
            info!("saving external images");
            let mut arc_map = FastHashMap::<*const u8, String>::default();
            let mut tex_map = FastHashMap::<u32, String>::default();
            let handler = self.external_image_handler
                .as_mut()
                .expect("Unable to lock the external image handler!");
            for def in &deferred_images {
                info!("\t{}", def.short_path);
                let ExternalImageData { id, channel_index, image_type } = def.external;
                // The image rendering parameter is irrelevant because no filtering happens during capturing.
                let ext_image = handler.lock(id, channel_index, ImageRendering::Auto);
                let (data, short_path) = match ext_image.source {
                    ExternalImageSource::RawData(data) => {
                        let arc_id = arc_map.len() + 1;
                        match arc_map.entry(data.as_ptr()) {
                            Entry::Occupied(e) => {
                                (None, e.get().clone())
                            }
                            Entry::Vacant(e) => {
                                let short_path = format!("externals/d{}.raw", arc_id);
                                (Some(data.to_vec()), e.insert(short_path).clone())
                            }
                        }
                    }
                    ExternalImageSource::NativeTexture(gl_id) => {
                        let tex_id = tex_map.len() + 1;
                        match tex_map.entry(gl_id) {
                            Entry::Occupied(e) => {
                                (None, e.get().clone())
                            }
                            Entry::Vacant(e) => {
                                let target = match image_type {
                                    ExternalImageType::TextureHandle(target) => target,
                                    ExternalImageType::Buffer => unreachable!(),
                                };
                                info!("\t\tnative texture of target {:?}", target);
                                let layer_index = 0; //TODO: what about layered textures?
                                self.device.attach_read_texture_external(gl_id, target, layer_index);
                                let data = self.device.read_pixels(&def.descriptor);
                                let short_path = format!("externals/t{}.raw", tex_id);
                                (Some(data), e.insert(short_path).clone())
                            }
                        }
                    }
                    ExternalImageSource::Invalid => {
                        info!("\t\tinvalid source!");
                        (None, String::new())
                    }
                };
                if let Some(bytes) = data {
                    fs::File::create(config.root.join(&short_path))
                        .expect(&format!("Unable to create {}", short_path))
                        .write_all(&bytes)
                        .unwrap();
                    #[cfg(feature = "png")]
                    CaptureConfig::save_png(
                        config.root.join(&short_path).with_extension("png"),
                        def.descriptor.size,
                        ReadPixelsFormat::Standard(def.descriptor.format),
                        &bytes,
                    );
                }
                let plain = PlainExternalImage {
                    data: short_path,
                    external: def.external,
                    uv: ext_image.uv,
                };
                config.serialize(&plain, &def.short_path);
            }
            for def in &deferred_images {
                handler.unlock(def.external.id, def.external.channel_index);
            }
        }

        if config.bits.contains(CaptureBits::FRAME) {
            let path_textures = config.root.join("textures");
            if !path_textures.is_dir() {
                fs::create_dir(&path_textures).unwrap();
            }

            info!("saving GPU cache");
            self.update_gpu_cache(); // flush pending updates
            let mut plain_self = PlainRenderer {
                framebuffer_size: self.framebuffer_size,
                gpu_cache: Self::save_texture(
                    &self.gpu_cache_texture.texture.as_ref().unwrap(),
                    "gpu", &config.root, &mut self.device,
                ),
                gpu_cache_frame_id: self.gpu_cache_frame_id,
                textures: FastHashMap::default(),
                external_images: deferred_images,
            };

            info!("saving cached textures");
            for (id, texture) in &self.texture_resolver.texture_cache_map {
                let file_name = format!("cache-{}", plain_self.textures.len() + 1);
                info!("\t{}", file_name);
                let plain = Self::save_texture(texture, &file_name, &config.root, &mut self.device);
                plain_self.textures.insert(*id, plain);
            }

            config.serialize(&plain_self, "renderer");
        }

        self.device.reset_read_target();
        self.device.end_frame();
        info!("done.");
    }

    #[cfg(feature = "replay")]
    fn load_capture(
        &mut self, root: PathBuf, plain_externals: Vec<PlainExternalImage>
    ) {
        use std::fs::File;
        use std::io::Read;
        use std::slice;

        info!("loading external buffer-backed images");
        assert!(self.texture_resolver.external_images.is_empty());
        let mut raw_map = FastHashMap::<String, Arc<Vec<u8>>>::default();
        let mut image_handler = DummyExternalImageHandler {
            data: FastHashMap::default(),
        };
        // Note: this is a `SCENE` level population of the external image handlers
        // It would put both external buffers and texture into the map.
        // But latter are going to be overwritten later in this function
        // if we are in the `FRAME` level.
        for plain_ext in plain_externals {
            let data = match raw_map.entry(plain_ext.data) {
                Entry::Occupied(e) => e.get().clone(),
                Entry::Vacant(e) => {
                    let mut buffer = Vec::new();
                    File::open(root.join(e.key()))
                        .expect(&format!("Unable to open {}", e.key()))
                        .read_to_end(&mut buffer)
                        .unwrap();
                    e.insert(Arc::new(buffer)).clone()
                }
            };
            let ext = plain_ext.external;
            let value = (CapturedExternalImageData::Buffer(data), plain_ext.uv);
            image_handler.data.insert((ext.id, ext.channel_index), value);
        }

        if let Some(renderer) = CaptureConfig::deserialize::<PlainRenderer, _>(&root, "renderer") {
            info!("loading cached textures");
            self.framebuffer_size = renderer.framebuffer_size;
            self.device.begin_frame();

            for (_id, texture) in self.texture_resolver.texture_cache_map.drain() {
                self.device.delete_texture(texture);
            }
            for (id, texture) in renderer.textures {
                info!("\t{}", texture.data);
                let t = Self::load_texture(
                    TextureTarget::Array,
                    &texture,
                    Some(RenderTargetInfo { has_depth: false }),
                    &root,
                    &mut self.device
                );
                self.texture_resolver.texture_cache_map.insert(id, t.0);
            }

            info!("loading gpu cache");
            if let Some(t) = self.gpu_cache_texture.texture.take() {
                self.device.delete_texture(t);
            }
            let (t, gpu_cache_data) = Self::load_texture(
                TextureTarget::Default,
                &renderer.gpu_cache,
                Some(RenderTargetInfo { has_depth: false }),
                &root,
                &mut self.device,
            );
            self.gpu_cache_texture.texture = Some(t);
            match self.gpu_cache_texture.bus {
                GpuCacheBus::PixelBuffer { ref mut rows, .. } => {
                    let dim = self.gpu_cache_texture.texture.as_ref().unwrap().get_dimensions();
                    let blocks = unsafe {
                        slice::from_raw_parts(
                            gpu_cache_data.as_ptr() as *const GpuBlockData,
                            gpu_cache_data.len() / mem::size_of::<GpuBlockData>(),
                        )
                    };
                    // fill up the CPU cache from the contents we just loaded
                    rows.clear();
                    rows.extend((0 .. dim.height).map(|_| CacheRow::new()));
                    let chunks = blocks.chunks(MAX_VERTEX_TEXTURE_WIDTH);
                    debug_assert_eq!(chunks.len(), rows.len());
                    for (row, chunk) in rows.iter_mut().zip(chunks) {
                        row.cpu_blocks.copy_from_slice(chunk);
                    }
                }
                GpuCacheBus::Scatter { .. } => {}
            }
            self.gpu_cache_frame_id = renderer.gpu_cache_frame_id;

            info!("loading external texture-backed images");
            let mut native_map = FastHashMap::<String, gl::GLuint>::default();
            for ExternalCaptureImage { short_path, external, descriptor } in renderer.external_images {
                let target = match external.image_type {
                    ExternalImageType::TextureHandle(target) => target,
                    ExternalImageType::Buffer => continue,
                };
                let plain_ext = CaptureConfig::deserialize::<PlainExternalImage, _>(&root, &short_path)
                    .expect(&format!("Unable to read {}.ron", short_path));
                let key = (external.id, external.channel_index);

                let tid = match native_map.entry(plain_ext.data) {
                    Entry::Occupied(e) => e.get().clone(),
                    Entry::Vacant(e) => {
                        //TODO: provide a way to query both the layer count and the filter from external images
                        let (layer_count, filter) = (1, TextureFilter::Linear);
                        let plain_tex = PlainTexture {
                            data: e.key().clone(),
                            size: (descriptor.size, layer_count),
                            format: descriptor.format,
                            filter,
                        };
                        let t = Self::load_texture(
                            target,
                            &plain_tex,
                            None,
                            &root,
                            &mut self.device
                        );
                        let extex = t.0.into_external();
                        self.owned_external_images.insert(key, extex.clone());
                        e.insert(extex.internal_id()).clone()
                    }
                };

                let value = (CapturedExternalImageData::NativeTexture(tid), plain_ext.uv);
                image_handler.data.insert(key, value);
            }

            self.device.end_frame();
        }

        self.output_image_handler = Some(Box::new(()) as Box<_>);
        self.external_image_handler = Some(Box::new(image_handler) as Box<_>);
        info!("done.");
    }
}

#[cfg(feature = "pathfinder")]
fn get_vao<'a>(vertex_array_kind: VertexArrayKind,
               vaos: &'a RendererVAOs,
               gpu_glyph_renderer: &'a GpuGlyphRenderer)
               -> &'a VAO {
    match vertex_array_kind {
        VertexArrayKind::Primitive => &vaos.prim_vao,
        VertexArrayKind::Clip => &vaos.clip_vao,
        VertexArrayKind::Blur => &vaos.blur_vao,
        VertexArrayKind::VectorStencil => &gpu_glyph_renderer.vector_stencil_vao,
        VertexArrayKind::VectorCover => &gpu_glyph_renderer.vector_cover_vao,
        VertexArrayKind::Border => &vaos.border_vao,
        VertexArrayKind::Scale => &vaos.scale_vao,
        VertexArrayKind::LineDecoration => &vaos.line_vao,
    }
}

#[cfg(not(feature = "pathfinder"))]
fn get_vao<'a>(vertex_array_kind: VertexArrayKind,
               vaos: &'a RendererVAOs,
               _: &'a GpuGlyphRenderer)
               -> &'a VAO {
    match vertex_array_kind {
        VertexArrayKind::Primitive => &vaos.prim_vao,
        VertexArrayKind::Clip => &vaos.clip_vao,
        VertexArrayKind::Blur => &vaos.blur_vao,
        VertexArrayKind::VectorStencil | VertexArrayKind::VectorCover => unreachable!(),
        VertexArrayKind::Border => &vaos.border_vao,
        VertexArrayKind::Scale => &vaos.scale_vao,
        VertexArrayKind::LineDecoration => &vaos.line_vao,
    }
}

#[derive(Clone, Copy, PartialEq)]
enum FramebufferKind {
    Main,
    Other,
}

