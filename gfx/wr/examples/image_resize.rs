/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

extern crate gleam;
extern crate glutin;
extern crate webrender;
extern crate winit;

#[path = "common/boilerplate.rs"]
mod boilerplate;
#[path = "common/image_helper.rs"]
mod image_helper;

use boilerplate::{Example, HandyDandyRectBuilder};
use webrender::api::*;

struct App {
    image_key: ImageKey,
}

impl Example for App {
    fn render(
        &mut self,
        _api: &RenderApi,
        builder: &mut DisplayListBuilder,
        txn: &mut Transaction,
        _framebuffer_size: FramebufferIntSize,
        pipeline_id: PipelineId,
        _document_id: DocumentId,
    ) {
        let (image_descriptor, image_data) = image_helper::make_checkerboard(32, 32);
        txn.add_image(
            self.image_key,
            image_descriptor,
            image_data,
            None,
        );

        let bounds = (0, 0).to(512, 512);
        let info = LayoutPrimitiveInfo::new(bounds);
        let space_and_clip = SpaceAndClipInfo::root_scroll(pipeline_id);

        builder.push_simple_stacking_context(
            &info,
            space_and_clip.spatial_id,
        );

        let image_size = LayoutSize::new(100.0, 100.0);

        let info = LayoutPrimitiveInfo::with_clip_rect(
            LayoutRect::new(LayoutPoint::new(100.0, 100.0), image_size),
            bounds,
        );
        builder.push_image(
            &info,
            &space_and_clip,
            image_size,
            LayoutSize::zero(),
            ImageRendering::Auto,
            AlphaType::PremultipliedAlpha,
            self.image_key,
            ColorF::WHITE,
        );

        let info = LayoutPrimitiveInfo::with_clip_rect(
            LayoutRect::new(LayoutPoint::new(250.0, 100.0), image_size),
            bounds,
        );
        builder.push_image(
            &info,
            &space_and_clip,
            image_size,
            LayoutSize::zero(),
            ImageRendering::Pixelated,
            AlphaType::PremultipliedAlpha,
            self.image_key,
            ColorF::WHITE,
        );

        builder.pop_stacking_context();
    }

    fn on_event(&mut self, event: winit::WindowEvent, api: &RenderApi, document_id: DocumentId) -> bool {
        match event {
            winit::WindowEvent::KeyboardInput {
                input: winit::KeyboardInput {
                    state: winit::ElementState::Pressed,
                    virtual_keycode: Some(winit::VirtualKeyCode::Space),
                    ..
                },
                ..
            } => {
                let mut image_data = Vec::new();
                for y in 0 .. 64 {
                    for x in 0 .. 64 {
                        let r = 255 * ((y & 32) == 0) as u8;
                        let g = 255 * ((x & 32) == 0) as u8;
                        image_data.extend_from_slice(&[0, g, r, 0xff]);
                    }
                }

                let mut txn = Transaction::new();
                txn.update_image(
                    self.image_key,
                    ImageDescriptor::new(64, 64, ImageFormat::BGRA8, true, false),
                    ImageData::new(image_data),
                    &DirtyRect::All,
                );
                let mut txn = Transaction::new();
                txn.generate_frame();
                api.send_transaction(document_id, txn);
            }
            _ => {}
        }

        false
    }
}

fn main() {
    let mut app = App {
        image_key: ImageKey(IdNamespace(0), 0),
    };
    boilerplate::main_wrapper(&mut app, None);
}
