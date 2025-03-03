/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*!
A GPU based renderer for the web.

It serves as an experimental render backend for [Servo](https://servo.org/),
but it can also be used as such in a standalone application.

# External dependencies
WebRender currently depends on [FreeType](https://www.freetype.org/)

# Api Structure
The main entry point to WebRender is the [`crate::Renderer`].

By calling [`Renderer::new(...)`](crate::Renderer::new) you get a [`Renderer`], as well as
a [`RenderApiSender`](api::RenderApiSender). Your [`Renderer`] is responsible to render the
previously processed frames onto the screen.

By calling [`yourRenderApiSender.create_api()`](api::RenderApiSender::create_api), you'll
get a [`RenderApi`](api::RenderApi) instance, which is responsible for managing resources
and documents. A worker thread is used internally to untie the workload from the application
thread and therefore be able to make better use of multicore systems.

## Frame

What is referred to as a `frame`, is the current geometry on the screen.
A new Frame is created by calling [`set_display_list()`](api::Transaction::set_display_list)
on the [`RenderApi`](api::RenderApi). When the geometry is processed, the application will be
informed via a [`RenderNotifier`](api::RenderNotifier), a callback which you pass to
[`Renderer::new`].
More information about [stacking contexts][stacking_contexts].

[`set_display_list()`](api::Transaction::set_display_list) also needs to be supplied with
[`BuiltDisplayList`](api::BuiltDisplayList)s. These are obtained by finalizing a
[`DisplayListBuilder`](api::DisplayListBuilder). These are used to draw your geometry. But it
doesn't only contain trivial geometry, it can also store another
[`StackingContext`](api::StackingContext), as they're nestable.

[stacking_contexts]: https://developer.mozilla.org/en-US/docs/Web/CSS/CSS_Positioning/Understanding_z_index/The_stacking_context
*/

// Cribbed from the |matches| crate, for simplicity.
macro_rules! matches {
    ($expression:expr, $($pattern:tt)+) => {
        match $expression {
            $($pattern)+ => true,
            _ => false
        }
    }
}

#[macro_use]
extern crate bitflags;
#[macro_use]
extern crate cfg_if;
#[macro_use]
extern crate cstr;
#[macro_use]
extern crate lazy_static;
#[macro_use]
extern crate log;
#[macro_use]
extern crate malloc_size_of_derive;
#[cfg(any(feature = "serde"))]
#[macro_use]
extern crate serde;
#[macro_use]
extern crate thread_profiler;

extern crate malloc_size_of;
extern crate svg_fmt;

#[macro_use]
mod profiler;

mod batch;
mod border;
mod box_shadow;
#[cfg(any(feature = "capture", feature = "replay"))]
mod capture;
mod clip;
mod clip_scroll_tree;
mod debug_colors;
mod debug_font_data;
mod debug_render;
#[cfg(feature = "debugger")]
mod debug_server;
mod device;
mod display_list_flattener;
mod ellipse;
mod filterdata;
mod frame_builder;
mod freelist;
#[cfg(any(target_os = "macos", target_os = "windows"))]
mod gamma_lut;
mod glyph_cache;
mod glyph_rasterizer;
mod gpu_cache;
#[cfg(feature = "pathfinder")]
mod gpu_glyph_renderer;
mod gpu_types;
mod hit_test;
mod image;
mod intern;
mod internal_types;
mod picture;
mod prim_store;
mod print_tree;
mod record;
mod render_backend;
mod render_task;
mod renderer;
mod resource_cache;
mod scene;
mod scene_builder;
mod screen_capture;
mod segment;
mod shade;
mod spatial_node;
mod storage;
mod texture_allocator;
mod texture_cache;
mod tiling;
mod util;

mod shader_source {
    include!(concat!(env!("OUT_DIR"), "/shaders.rs"));
}

pub use crate::record::{ApiRecordingReceiver, BinaryRecorder, WEBRENDER_RECORDING_HEADER};

mod platform {
    #[cfg(target_os = "macos")]
    pub use crate::platform::macos::font;
    #[cfg(any(target_os = "android", all(unix, not(target_os = "macos"))))]
    pub use crate::platform::unix::font;
    #[cfg(target_os = "windows")]
    pub use crate::platform::windows::font;

    #[cfg(target_os = "macos")]
    pub mod macos {
        pub mod font;
    }
    #[cfg(any(target_os = "android", all(unix, not(target_os = "macos"))))]
    pub mod unix {
        pub mod font;
    }
    #[cfg(target_os = "windows")]
    pub mod windows {
        pub mod font;
    }
}

#[cfg(target_os = "macos")]
extern crate core_foundation;
#[cfg(target_os = "macos")]
extern crate core_graphics;
#[cfg(target_os = "macos")]
extern crate core_text;

#[cfg(all(unix, not(target_os = "macos")))]
extern crate freetype;
#[cfg(all(unix, not(target_os = "macos")))]
extern crate libc;

#[cfg(target_os = "windows")]
extern crate dwrote;

extern crate bincode;
extern crate byteorder;
pub extern crate euclid;
extern crate fxhash;
extern crate gleam;
extern crate num_traits;
#[cfg(feature = "pathfinder")]
extern crate pathfinder_font_renderer;
#[cfg(feature = "pathfinder")]
extern crate pathfinder_gfx_utils;
#[cfg(feature = "pathfinder")]
extern crate pathfinder_partitioner;
#[cfg(feature = "pathfinder")]
extern crate pathfinder_path_utils;
extern crate plane_split;
extern crate rayon;
#[cfg(feature = "ron")]
extern crate ron;
#[cfg(feature = "debugger")]
extern crate serde_json;
extern crate sha2;
#[macro_use]
extern crate smallvec;
extern crate time;
#[cfg(feature = "debugger")]
extern crate ws;
#[cfg(feature = "debugger")]
extern crate image_loader;
#[cfg(feature = "debugger")]
extern crate base64;
#[cfg(all(feature = "capture", feature = "png"))]
extern crate png;
#[cfg(test)]
extern crate rand;

#[macro_use]
pub extern crate api;
extern crate webrender_build;

#[doc(hidden)]
pub use crate::device::{build_shader_strings, UploadMethod, VertexUsageHint};
pub use crate::device::{ProgramBinary, ProgramCache, ProgramCacheObserver};
pub use crate::device::Device;
pub use crate::frame_builder::ChasePrimitive;
pub use crate::profiler::{ProfilerHooks, set_profiler_hooks};
pub use crate::renderer::{
    AsyncPropertySampler, CpuProfile, DebugFlags, OutputImageHandler, RendererKind, ExternalImage,
    ExternalImageHandler, ExternalImageSource, GpuProfile, GraphicsApi, GraphicsApiInfo,
    PipelineInfo, Renderer, RendererOptions, RenderResults, RendererStats, SceneBuilderHooks,
    ThreadListener, ShaderPrecacheFlags, MAX_VERTEX_TEXTURE_WIDTH,
};
pub use crate::screen_capture::{AsyncScreenshotHandle, RecordedFrameHandle};
pub use crate::shade::{Shaders, WrShaders};
pub use api as webrender_api;
pub use webrender_build::shader::ProgramSourceDigest;
