import React, { useEffect, useRef, useState } from 'react';

const ShaderCanvas = ({ keys, onWasmLoaded }) => {
  const wasmInstance = useRef(null);
  const canvasRef = useRef(null);
  
  // Track the actual window size natively in React
  const [size, setSize] = useState({ w: window.innerWidth, h: window.innerHeight });

  // Tracking pointer state for dragging
  const isDragging = useRef(false);
  const lastX = useRef(0);

  useEffect(() => {
    // Update the canvas resolution if the user resizes their browser window. 
    // This does not work perfectly but im over dealing with it.
    const handleResize = () => setSize({ w: window.innerWidth, h: window.innerHeight });
    window.addEventListener('resize', handleResize);
    return () => window.removeEventListener('resize', handleResize);
  }, []);

  useEffect(() => {
    if (window.createRenderer) {
      window.createRenderer({
        canvas: canvasRef.current,
        locateFile: (path) => {
          if (path.endsWith('.wasm')) {
            return `${process.env.PUBLIC_URL}/wasm/${path}`; 
          }
          return path;
        },
        print: (text) => console.log(`WASM: ${text}`),
        printErr: (text) => console.error(`WASM Error: ${text}`)
      }).then((module) => {
        wasmInstance.current = module;
        console.log("WASM execution started");
        
        // Trigger map parser
        if (onWasmLoaded) {
            onWasmLoaded(module);
        }
      });
    }
  }, []);

  useEffect(() => {
    if (wasmInstance.current && wasmInstance.current._update_input) {
      wasmInstance.current._update_input(
        keys.w ? 1 : 0, keys.s ? 1 : 0, 
        keys.a ? 1 : 0, keys.d ? 1 : 0, 
        keys.q ? 1 : 0, keys.e ? 1 : 0
      );
    }
  }, [keys]);

  // Pointer Drag 
  const handlePointerDown = (e) => {
    isDragging.current = true;
    lastX.current = e.clientX;
    // Capture the pointer so tracking continues even if they drag slightly off the canvas. Which i do. Every time.
    e.target.setPointerCapture(e.pointerId); 
  };

  const handlePointerMove = (e) => {
    if (!isDragging.current || !wasmInstance.current) return;
    
    const deltaX = e.clientX - lastX.current;
    lastX.current = e.clientX;
    
    // Multiply by a sensitivity factor. May need to make this a setting, prefrence based really but 0.005 is fine for me.
    wasmInstance.current._add_cam_yaw(deltaX * 0.005);
  };

  const handlePointerUp = (e) => {
    isDragging.current = false;
    e.target.releasePointerCapture(e.pointerId);
  };

  return (
    <canvas 
      id="canvas" 
      ref={canvasRef}
      width={size.w}   // React explicitly sets the WebGL buffer width
      height={size.h}  // React explicitly sets the WebGL buffer height
      onPointerDown={handlePointerDown}
      onPointerMove={handlePointerMove}
      onPointerUp={handlePointerUp}
      onPointerCancel={handlePointerUp}
      style={{ 
        display: 'block',
        background: '#000',
        touchAction: 'none' // Stops mobile browsers from pull-to-refreshing when i drag
      }} 
    />
  );
};

export default ShaderCanvas;