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
        let view = CoreGraphicsView(frame: NSRect(x: 0, y: 0, width: 1024, height: 768))
        view.window?.makeFirstResponder(view) // Attempt to set first responder early
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        nsView.setNeedsDisplay(nsView.bounds)
    }
}

class CoreGraphicsView: NSView {
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
        shared_init(0, UInt.random(in: 0 ..< 4294967295))
    }

    @objc private func handleAppWillTerminate(notification: Notification) {
        print("App is quitting – perform cleanup here if needed.")
    }

    @objc private func handleWindowWillClose(notification: Notification) {
        print("Window is closing – perform any per-window logic here.")
    }
    
    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)
        
        guard let context = NSGraphicsContext.current?.cgContext else { return }
        let time_info : UInt = UInt(CFAbsoluteTimeGetCurrent())
        _ = shared_cycle(time_info, 0)
        context.saveGState()
        var dimY = Int(dirtyRect.height)
        let dimX = Int(dirtyRect.width)
        
//        if #available(macOS 14, *) {
//            dimY -= 28
//        }
        let  colorSpace: CGColorSpace = CGColorSpaceCreateDeviceRGB();
        let optionalDrawRef: CGContext? = CGContext.init(data: shared_draw(0, dimX, dimY, 0), width: dimX, height: dimY, bitsPerComponent: 8, bytesPerRow: dimX * 4, space: colorSpace, bitmapInfo: UInt32(CGBitmapInfo.byteOrder32Big.rawValue | CGImageAlphaInfo.noneSkipFirst.rawValue))
        
        if let drawRef = optionalDrawRef {
            context.setBlendMode(.normal)
            context.setShouldAntialias(false)
            context.setAllowsAntialiasing(false)
            let optionalImage: CGImage? = drawRef.makeImage()
            if let image = optionalImage {
                let newRect = NSRect(x:0, y:0, width:CGFloat(dimX), height:CGFloat(dimY))
                context.draw(image, in: newRect)
            }
        }
        context.restoreGState()
        
        DispatchQueue.main.async { [weak self] in
            self?.needsDisplay = true
        }
    }

    // MARK: - Input Handling
/*
    override var acceptsFirstResponder: Bool { true }

    override func becomeFirstResponder() -> Bool {
        print("View became first responder.")
        return true
    }

    override func resignFirstResponder() -> Bool {
        print("View resigned first responder.")
        return true
    }

    // Keyboard
    override func keyDown(with event: NSEvent) {
        print("Key down: \(event.keyCode) (characters: \(event.charactersIgnoringModifiers ?? ""))")
    }

    override func keyUp(with event: NSEvent) {
        print("Key up: \(event.keyCode)")
    }

    // Mouse
    override func mouseDown(with event: NSEvent) {
        print("Mouse down at: \(convert(event.locationInWindow, from: nil))")
    }

    override func mouseUp(with event: NSEvent) {
        print("Mouse up at: \(convert(event.locationInWindow, from: nil))")
    }

    override func mouseDragged(with event: NSEvent) {
        print("Mouse dragged to: \(convert(event.locationInWindow, from: nil))")
    }

    override func mouseMoved(with event: NSEvent) {
        print("Mouse moved to: \(convert(event.locationInWindow, from: nil))")
    }

    // Setup tracking for mouseMoved events
    private func setupTrackingArea() {
        let trackingArea = NSTrackingArea(rect: bounds,
                                          options: [.mouseMoved, .activeInKeyWindow, .inVisibleRect],
                                          owner: self,
                                          userInfo: nil)
        addTrackingArea(trackingArea)
    }
 */
}
