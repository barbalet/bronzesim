/****************************************************************

 CoreGraphicView.c

 =============================================================

 Copyright 1996-2025 Tom Barbalet. All rights reserved.

 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or
 sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following
 conditions:

 The above copyright notice and this permission notice shall be
 included in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.

 ****************************************************************/

import SwiftUI

struct CoreGraphicsViewRepresentable: NSViewRepresentable {
    func makeNSView(context: Context) -> NSView {
        let view = CoreGraphicsView(frame: NSRect(x: 0, y: 0, width: 1024, height: 800))
        view.window?.makeFirstResponder(view) // Attempt to set first responder early
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        nsView.setNeedsDisplay(nsView.bounds)
    }
}

class CoreGraphicsView: NSView {
    
    private var scenarioLoaded: Bool = false
override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupObservers()
        //setupTrackingArea()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupObservers()
        //setupTrackingArea()
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    private func setupObservers() {
        NotificationCenter.default.addObserver(self,
                                               selector: #selector(handleAppWillTerminate),
                                               name: NSApplication.willTerminateNotification,
                                               object: nil)

        NotificationCenter.default.addObserver(self,
                                               selector: #selector(handleWindowWillClose),
                                               name: NSWindow.willCloseNotification,
                                               object: nil)
        brz_shared_init(UInt.random(in: 0 ..< 4294967295))

        // Load example.bronze from the app bundle (if present) and build the realtime sim state.
        if !scenarioLoaded {
            scenarioLoaded = true
            if let url = Bundle.main.url(forResource: "example", withExtension: "bronze") {
                url.path.withCString { cstr in
                    _ = brz_shared_load_config(cstr)
                }
            } else {
                print("Warning: example.bronze not found in bundle")
            }
        }
    }

    @objc private func handleAppWillTerminate(notification: Notification) {
        print("App is quitting – perform cleanup here if needed.")
        brz_shared_close()
    }

    @objc private func handleWindowWillClose(notification: Notification) {
        print("Window is closing – perform any per-window logic here.")
    }
    
    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)
        
        guard let context = NSGraphicsContext.current?.cgContext else { return }
                // Pass milliseconds (UInt) to C so it can do stable step scheduling.
        let timeMs: UInt = UInt((CFAbsoluteTimeGetCurrent() * 1000.0).rounded())
        brz_shared_cycle(timeMs)
        context.saveGState()
                let dimX = 1024
        let dimY = 800
        
        let  colorSpace: CGColorSpace = CGColorSpaceCreateDeviceRGB();
        let optionalDrawRef: CGContext? = CGContext.init(data: brz_shared_draw(dimX, dimY), width: dimX, height: dimY, bitsPerComponent: 8, bytesPerRow: dimX * 4, space: colorSpace, bitmapInfo: UInt32(CGBitmapInfo.byteOrder32Big.rawValue | CGImageAlphaInfo.noneSkipFirst.rawValue))
        
        if let drawRef = optionalDrawRef {
            context.setBlendMode(.normal)
            context.setShouldAntialias(false)
            context.setAllowsAntialiasing(false)
            let optionalImage: CGImage? = drawRef.makeImage()
            if let image = optionalImage {
                                // Draw the fixed 1024x800 framebuffer into the current view bounds.
                context.draw(image, in: self.bounds)
            }
        }
        context.restoreGState()
        
        DispatchQueue.main.async { [weak self] in
            self?.needsDisplay = true
        }
    }

}