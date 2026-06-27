import React, { useState, useEffect, useRef } from 'react';
import ShaderCanvas from './components/ShaderCanvas';

// Need to probably load a map later. 
const DEFAULT_MAP = `
  _________
 |         |
 |    +    |
 |         |
 |___   ___|
     | |
     |_|
`;

const App = () => {
  // Game State
  const [isPlaying, setIsPlaying] = useState(false);
  const [isWasmReady, setIsWasmReady] = useState(false);
  const [mapInput, setMapInput] = useState(DEFAULT_MAP);
  const wasmModuleRef = useRef(null);

  // Input State
  const [keys, setKeys] = useState({
    w: false, a: false, s: false, d: false, q: false, e: false
  });

  useEffect(() => {
    const handleKeyDown = (e) => {
      const key = e.key.toLowerCase();
      if (keys.hasOwnProperty(key) && !keys[key]) {
        setKeys(prev => ({ ...prev, [key]: true }));
      }
    };
    const handleKeyUp = (e) => {
      const key = e.key.toLowerCase();
      if (keys.hasOwnProperty(key)) {
        setKeys(prev => ({ ...prev, [key]: false }));
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('keyup', handleKeyUp);

    return () => {
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('keyup', handleKeyUp);
    };
  }, [keys]);

  // WASM Map. Could be done better, also need to use a switch for the text as i want to add more stuff to it.
  // like different walls, maybe some enemies etc.
  const handleWasmLoaded = (wasmModule) => {
    wasmModuleRef.current = wasmModule;
    setIsWasmReady(true);
  };

  const launchGame = () => {
    const wasmModule = wasmModuleRef.current;
    if (!wasmModule || !wasmModule._add_wall) return;
    
    wasmModule._clear_map();
    // Default spawn in case the user deletes the '+' character
    wasmModule._set_player_pos(64.0, 64.0); 

    // Parse the user's live textarea input instead of the hardcoded string
    const lines = mapInput.split('\n').filter(line => line.length > 0);
    const TILE_SIZE = 128.0;

    for (let y = 0; y < lines.length; y++) {
      for (let x = 0; x < lines[y].length; x++) {
        const char = lines[y][x];
        
        // Base coordinates for the top left of this current text cell
        const worldX = x * TILE_SIZE;
        const worldZ = y * TILE_SIZE;

        if (char === '|') {
          // Vertical Wall, drawing down the left edge of the cell
          wasmModule._add_wall(worldX, worldZ, worldX, worldZ + TILE_SIZE, 2, 200.0);
        } 
        else if (char === '_' || char === '-') {
          // Horizontal Wall. Push to the bottom of the cell, and stretch it left and right 
          // by a full TILE_SIZE to completely close the ASCII gaps at the corners as right now they just stop which makes me sad.
          wasmModule._add_wall(
            worldX - TILE_SIZE, worldZ + TILE_SIZE, 
            worldX + TILE_SIZE, worldZ + TILE_SIZE, 
            2, 160.0
          );
        } 
        else if (char === '+') {
          // player spawn center.
          wasmModule._set_player_pos(worldX + (TILE_SIZE/2), worldZ + (TILE_SIZE/2));
        }
      }
    }
    
    console.log("Custom Map Loaded");
    setIsPlaying(true); // switches the UI from Menu to Game
  };

  // Styles
  const themeColor = 'rgb(255, 255, 255)'; // I'm boring.

  const menuStyle = {
    position: 'absolute',
    top: '50%',
    left: '50%',
    transform: 'translate(-50%, -50%)',
    background: 'rgba(0, 20, 0, 0.9)',
    padding: '30px',
    borderRadius: '8px',
    color: themeColor,
    border: `2px solid ${themeColor}`,
    fontFamily: 'monospace',
    zIndex: 30,
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    boxShadow: '0px 0px 20px rgba(0, 255, 0, 0.2)'
  };

  const hudStyle = {
    position: 'absolute',
    top: '20px',
    left: '20px',
    background: 'rgba(0, 0, 0, 0.5)',
    padding: '20px',
    borderRadius: '8px',
    color: themeColor,
    fontFamily: 'monospace',
    pointerEvents: 'none',  // Lets mouse clicks pass through to the canvas if needed later
    zIndex: 10
  };

  return (
    <div style={{ margin: 0, overflow: 'hidden', height: '100vh', background: '#000' }}>
      
      {/* menu shows before playing*/}
      {!isPlaying && (
        <div style={menuStyle}>
          <h1 style={{ margin: '0 0 10px 0' }}>MANIFOLD</h1>
          <p>Draw your silly WAD map mate. Use <b>|</b> and <b>_</b> for walls. Use <b>+</b> for spawn.</p>
          
          <textarea 
            value={mapInput}
            onChange={(e) => setMapInput(e.target.value)}
            spellCheck="false"
            style={{
              width: '400px',
              height: '250px',
              background: '#000',
              color: themeColor,
              border: `1px solid ${themeColor}`,
              padding: '10px',
              fontFamily: 'monospace',
              fontSize: '16px',
              resize: 'none',
              marginBottom: '20px',
              outline: 'none'
            }}
          />

          <button 
            onClick={launchGame}
            disabled={!isWasmReady}
            style={{
              padding: '15px 40px',
              fontSize: '20px',
              fontWeight: 'bold',
              background: isWasmReady ? 'rgba(0, 255, 0, 0.2)' : '#333',
              color: isWasmReady ? themeColor : '#666',
              border: `2px solid ${isWasmReady ? themeColor : '#666'}`,
              cursor: isWasmReady ? 'pointer' : 'not-allowed',
              fontFamily: 'monospace',
              textTransform: 'uppercase'
            }}
          >
            {isWasmReady ? 'Initialize Grid' : 'Loading Engine...'}
          </button>
        </div>
      )}

      {/* game, shows while playing */}
      {isPlaying && (
        <>
          <div style={hudStyle}>
            <h3>Map Controls Are:</h3>
            <p><b>W/S</b> : Forward / Backward</p>
            <p><b>A/D</b> : Strafe Left / Right</p>
            <p>Drag Screen to Look</p>
          </div>

          {/* Mobile Forward Button */}
          <button 
            onPointerDown={() => setKeys(prev => ({ ...prev, w: true }))}
            onPointerUp={() => setKeys(prev => ({ ...prev, w: false }))}
            onPointerLeave={() => setKeys(prev => ({ ...prev, w: false }))}
            style={{
              position: 'absolute',
              bottom: '40px',
              left: '50%',
              transform: 'translateX(-50%)',
              padding: '20px 60px',
              fontSize: '24px',
              fontWeight: 'bold',
              borderRadius: '50px',
              background: 'rgba(0, 255, 0, 0.2)',
              color: themeColor,
              border: `2px solid ${themeColor}`,
              touchAction: 'none',
              pointerEvents: 'auto',
              cursor: 'pointer',
              zIndex: 20,
              userSelect: 'none' 
            }}
          >
            FORWARD
          </button>
        </>
      )}

      {/* the engine is always loaded, hidden behind menu if not playing */}
      <ShaderCanvas 
        keys={keys} 
        onWasmLoaded={handleWasmLoaded} // pass this callback to the canvas
      />
    </div>
  );
};

export default App;