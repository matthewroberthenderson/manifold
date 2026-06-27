#include <GLES2/gl2.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <iostream>
#include <cmath>

EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_ctx;


// Global State
GLuint g_program;
GLuint g_vbo;
float g_time = 0.0f;

// Camera / Movement State
float g_cam_x = 0.0f;
float g_cam_y = 0.0f;
float g_cam_z = 0.0f;
float g_cam_yaw = 0.0f;

// Input States from the js side.
int in_f = 0, in_b = 0, in_l = 0, in_r = 0, in_tl = 0, in_tr = 0;

// Dynamic Map Memory (not really very dynamic but whatever)
#define MAX_LINES 80
int g_num_lines = 0;
float g_line_coords[MAX_LINES * 4]; // x1, y1, x2, y2
float g_line_props[MAX_LINES * 2];  // material, lighting

// Can probably use whatever you want from online.
// This is bad, should load this from files.
const char* vs_source = R"(
    attribute vec2 a_position;
    void main() {
        gl_Position = vec4(a_position, 0.0, 1.0);
    }
)";

const char* fs_source = R"(
    #ifdef GL_FRAGMENT_PRECISION_HIGH
      precision highp float;
    #else
      precision mediump float;
    #endif

    uniform vec2 u_resolution;
    uniform float u_time;
    uniform vec3 u_camPos;
    uniform float u_camYaw;

    // Dynamic Map Uniforms
    uniform int u_numLines;
    uniform vec4 u_lineCoords[80];
    uniform vec2 u_lineProps[80];
    

    #define COL(r,g,b) vec3(r/255.,g/255.,b/255.)

    float hash( const float n ) {
        return fract(sin(n*14.1234512)*51231.545341231);
    }
    float hash( const vec2 x ) {
        float n = dot( x, vec2(14.1432,1131.15532) );
        return fract(sin(n)*51231.545341231);
    }
    float crossp( const vec2 a, const vec2 b ) { return a.x*b.y - a.y*b.x; }
    
    bool intersectWall(const vec3 ro, const vec3 rd, const vec2 a, const vec2 b, const float height, 
                       inout float dist, inout vec2 uv ) {
        vec2 p = ro.xz; vec2 r = rd.xz;
        vec2 q = a-p;   vec2 s = b-a;
        float rCrossS = crossp(r, s);
        if( rCrossS == 0.) return false;
        
        float d = crossp(q, s) / rCrossS;
        float u = crossp(q, r) / rCrossS;
        float he = ro.y+rd.y*d;
        
        if(0. <= d && d < dist && 0. <= u && u <= 1. && he*sign(height) < height ) {
            dist = d;
            uv = vec2( -u*length(s), height-he );
            return true;
        }
        return false;
    }

    bool intersectFloor(const vec3 ro, const vec3 rd, const float height, 
                        inout float dist, inout vec2 uv ) { 
        if (rd.y==0.0) return false;
        float d = -(ro.y - height)/rd.y;
        d = min(100000.0, d);
        if( d > 0. && d < dist) {
            dist = d;
            uv = ro.xz+dist*rd.xz;
            return true;
        }
        return false;
    }

    float sat( const float a ) { return clamp(a,0.,1.); }
    float onCircleAA( const vec2 c, const vec2 centre, const float radius, const float aa ) {
        return sat( aa*(radius - distance(c,centre)) );
    }
    float onLineX( const vec2 c, const float x ) { return step(x,c.x)*step(c.x,x); }
    float onLineY( const vec2 c, const float y ) { return step(y,c.y)*step(c.y,y); }
    float onBand( const float c, const float mi, const float ma ) { return step(mi,c)*step(c,ma); }
    float onRect( const vec2 c, const vec2 lt, const vec2 rb ) {
        return onBand( c.x, lt.x, rb.x )*onBand( c.y, lt.y, rb.y );
    }
    vec3 addKnobAA( const vec2 c, const vec2 centre, const float radius, const float strength, const vec3 col ) {
        vec2 lv = normalize( centre-c );
        return mix( col, col*(1.0+strength*dot(lv,vec2(-0.7071,0.7071))), onCircleAA(c, centre, radius, 4. ) );
    }
    float onBandAA( const float c, const float mi, const float ma ) {
        return sat( (ma-c+1.) )*sat( (c-mi+1.) );
    }
    float onRectAA( const vec2 c, const vec2 lt, const vec2 rb ) {
        return onBandAA( c.x, lt.x, rb.x )*onBandAA( c.y, lt.y, rb.y );
    }
    vec3 addBevel( const vec2 c, const vec2 lt, const vec2 rb, const float size, const float strength, const float lil, const float lit, const vec3 col ) {
        float xl = sat( (c.x-lt.x)/size); 
        float xr = sat( (rb.x-c.x)/size);   
        float yt = sat( (c.y-lt.y)/size); 
        float yb = sat( (rb.y-c.y)/size);
        return mix( col, col*clamp(1.0+strength*(lil*(xl-xr)+lit*(yb-yt)), 0., 2.), onRectAA( c, lt, rb ) );
    }

    void getMaterialColor( const int material, in vec2 uv, out vec3 col ) { 
        uv = floor( uv );
        float huv = hash(uv), huvx = hash(uv.x);
        
        if( material == 0 ) {
            uv = mod(uv, vec2(64.)); vec2 centre = mod(uv,vec2(32.,16.));
            col = mix( COL(90.,98.,69.),COL(152.,149.,125.),(0.75*huv+0.25*mod(uv.x,2.)) );
            col = mix( col, mix(vec3(243./255.),vec3(169./255.), distance(centre,vec2(16.,8.))/6.5), onCircleAA(centre, vec2(16.,8.), 6.25, 0.75) );
        } 
        else if( material == 1 ) {
            uv = mod(uv, vec2(64.)); vec2 uv8 = mod(uv, vec2(32.,7.7));
            float h = huv*huvx;
            col = mix( COL(136.,114.,95.), COL(143.,122.,92.), sat(4.*h) ); 
            col = mix( col, COL(175.,126.,89.), sat( 2.*(hash(floor(uv*0.125))+huv-1.35) ) );
            col = mix( col, COL(121.,103.,83.), sat( onLineX(uv,0.)+onLineY(uv,63.)) );
            col = mix( col, COL(121.,103.,83.), onLineX(uv,31.)*huv );
            uv8.x = abs(16.-uv8.x);
            float d = min( max( uv8.x-8.,abs(uv8.y-4.) ), abs(distance(uv8,vec2(11.,4.))) )+huv;
            vec3 fgcol = mix( col, col*sat(((16.-uv8.y)/12.)), step(d,3.) );
            col = mix( mix( fgcol, COL(114.,94.,78.), sat(d*(3.5-d)/4.)*step(2.,d) ), col, onRect(uv, vec2(32.,23.),vec2(63.,39.) ) );
        }
        else if( material == 2 ) {
            uv = mod(uv, vec2(128.,128.)); vec2 uv64 = mod(uv, vec2(64.,65.) ); vec2 uv24 = mod(uv64, vec2(64.,24.) );
            float h = huv*huvx;
            col = mix( vec3(114./255.), vec3(98./255.), sat(2.*h) );
            col = mix( col, mix( COL(111.,114.,87.), COL(90.,98.,69.), sat(2.*h) ), sat( 100.*(hash(uv+vec2(523.,53.))*hash(150.-uv.x)-0.15)) );  
            col = addKnobAA( mod( uv24, vec2(3.,32.) ), vec2(0.,4.5), 1.1, 0.4, col );
            col = mix( col, COL(137.,141.,115.), 0.7*sat( onLineX(uv64,1.)+onLineY(uv,1.)+onLineY(uv24,0.)+onLineY(uv24,19.)+onLineY(uv64,59.) ) ); 
            col = mix( col, COL(73.,81.,55.), sat( onLineX(uv64,0.)+onLineX(uv64,62.) ) ); 
            col = mix( col, mix(COL(73.,81.,55.),vec3(38./255.),uv24.y-22.), onBand(uv24.y,22.,23.) ); 
            col = mix( col, mix(COL(73.,81.,55.),vec3(38./255.),uv64.y-63.), onBand(uv64.y,63.,64.) ); 
            col = mix( col, vec3(38./255.), sat( onLineY(uv,0.)+onLineX(uv64,63.) ) ); 
            col = mix( col, COL(137.,141.,115.), onRect(uv,vec2(3.),vec2(60.,12.)) ); 
            col = mix( col, mix( vec3(1.), COL(255.,253.,110.), sat( abs(uv.x-32.)/20.)-0.25*mod(uv.x,2.)), onRect(uv,vec2(4.),vec2(59.,11.)) ); 
        }   
        else if( material == 3 ) {
            uv = mod(uv, vec2(64.,128.)); float s = sin(31.15926*uv.x/64.);
            col = mix( vec3(75./255.), vec3(64./255.), huv );
            col = mix( col, COL(106.,86.,51.),  sat( 5.*(huv+(s+1.2)*(1.-(uv.y+44.)/64.))) * onBand(uv.y, 0., 30. ) );
            col = mix( col, COL(123.,105.,85.), sat( 2.*(0.5*huvx+huv+(s+1.7)*(1.-(uv.y+44.)/64.)-0.5) ) * onBand(uv.y, 0., 30. ) );
            col = mix( col, COL(106.,86.,51.),  sat( 5.*(huv+(s+0.7)*(1.-(uv.y+14.)/64.))) * onBand(uv.y, 30., 98. ) );
            col = mix( col, COL(123.,105.,85.), sat( 2.*(1.1*huvx+(s+1.7)*(1.-(uv.y+14.)/64.)-0.5) ) * onBand(uv.y, 30., 98. ) );
            col = mix( col, COL(7.,59.,20.), sat( huv*uv.y/96.-0.5) );
            col = mix( col, COL(106.,86.,51.),  sat( 5.*(huv+(s+1.2)*(1.-(uv.y-40.)/64.))) * onBand(uv.y, 98., 128. ) );
            col = mix( col, COL(123.,105.,85.), sat( 2.*(huvx+(s+1.7)*(1.-(uv.y-40.)/64.)-0.5) ) * onBand(uv.y, 98., 128. ) );   
            col = mix( col, mix(COL(110.,89.,70.),COL(130.,112.,92.),sat((uv.y-3.)/18.)), onRectAA(mod(uv,vec2(16.,128.)),vec2(6.5,1.5),vec2(12.5,21.5)) );
            col = addBevel( mod(uv,vec2(16.,128.)),vec2(5.5,-2.5),vec2(12.5,21.5), 2.3, 1., 0.1, 0.7, col );
            col = mix( col, addBevel( abs(mod(uv+vec2(0.,-85.),vec2(64.))-vec2(32.,0.)), vec2(15.5,0.5), vec2(34.5,52.5), 1.2, 1., 0.5, -0.7, col ), onBand(uv.y, 30.,97.));
            col = mix( col, 0.7*col, sat( onLineY(uv,127.)+onLineX(uv,0.)+onBand(uv.y, 97.,98.)+onBand(uv.y, 29.,30.)) );
            col = mix( col, 1.2*col, sat( onBand(uv.y, 98.,99.)+onBand(uv.y, 0.,1.)+onLineX(uv, 63.)) );
            col = mix( col, 0.75*col*uv.x, onBand(uv.x, 0., 1.)*onBand(uv.y, 30.,97.) );
            col *= 1.0-0.1*huv;
        }   
        else if( material == 4 ) {
            uv = mod(uv, vec2(64.,16.));
            col = mix( COL(182.,133.,93.), COL(132.,98.,66.), sat(huv-0.5) );
            col = mix( col, COL(129.,111.,79.), sat(1.-(uv.y-4.)/8.) );
            col = mix( col, COL(102.,82.,50.), sat((huv+1.)*onRectAA(mod(uv,vec2(32.,16.)), vec2(1.5,9.7), vec2(29.5,13.5))) );
            col = mix( col, COL(102.,82.,50.), 0.6*sat((huv+1.)*onRectAA(mod(uv,vec2(8.,16.)), vec2(2.5,3.5), vec2(5.5,6.2))) );
            col = mix( col, COL(143.,122.,92.), onLineY(uv,0.) );
            col = mix( col, COL(106.,86.,61.), onLineY(uv,2.) );
            col *= 1.-0.2*onLineY(uv,3.);
        }
        else if( material == 5 ) {
            uv = mod(uv, vec2(128.,64.)); float huv2 = hash( uv*5312. );
            col = mix( mix(COL(184.,165.,144.),COL(136.,102.,67.),uv.x/128.), 
                       mix(COL(142.,122.,104.),COL(93.,77.,50.),uv.x/128.), sat(huv+huvx) );
            col *= 1.+0.5*sat(hash(uv.y)-0.7);
            col *= 1.-0.2*sat(hash(uv.y-1.)-0.7);
            col = mix( col, COL(102.,82.,50.), sat(0.2*huv2+3.*(huvx-0.7)) );
            col = mix( col, COL(165.,122.,85.), (0.75+0.5*huv2)*sat( onBandAA(uv.x,122.5,123.5)+onBandAA(uv.x,117.5,118.5)+onBandAA(uv.x,108.5,109.5) ) );
            col = mix( col, mix(  (1.-sat(0.2*abs(2.8-mod(uv.x,6.))))*mix(COL(175.,126.,89.),COL(143.,107.,71.),0.4*distance( mod(uv,vec2(6.)), vec2 (1.5))), COL(77.,68.,40.), onBandAA(mod(uv.x+1.,6.),0.,1.5)),
                                   (0.75+0.5*huv2)*sat( onBandAA(uv.x,6.5,11.5)+onBandAA(uv.x,54.5,59.5)+onBandAA(uv.x,66.5,70.5)+onBandAA(uv.x,72.5,78.5) ) );
            col = mix( col, mix( COL(82.,90.,64.), 1.2*COL(118.,125.,99.), huv*(sat(abs(uv.x-14.)-huv)+sat(abs(uv.x-62.)-huv)) ), onBandAA(uv.x,12.8,13.8) + onBandAA(uv.x,60.8,61.8));
            col = mix( col, vec3(0.), 0.3*(onBandAA(uv.y,18.8,21.8)*onBandAA(uv.x,40.8,52.8) + onBandAA(uv.x,0.1,3.7) + onBandAA(uv.x,41.3,44.2) + onBandAA(uv.x,48.9,51.8)+0.6*onBandAA(uv.x,80.1,81.6)));
            col = mix( col, mix( 1.2*COL(205.,186.,167.), COL(143.,122.,92.), 0.3*(sat(abs(uv.x-2.)+huv)+sat(abs(uv.x-43.)+huv)+sat(abs(uv.x-51.)+huv)) ), onBandAA(uv.x,0.8,2.8) + onBandAA(uv.x,42.1,43.3) + onBandAA(uv.x,49.8,51.2)+0.6*onBandAA(uv.x,80.8,81.5));
            col = mix( col, mix( 1.2*COL(205.,186.,167.), COL(154.,133.,105.), (sat(abs(uv.y-20.5)+huv)) ), onBandAA(uv.y,19.3,21.2)*onBandAA(uv.x,40.8,52.1));
            float d = min( min( min( min( min( min( distance(uv,vec2(6.,39.)), 0.8*distance(uv,vec2(23.,45.)) ), 1.2*distance(uv,vec2(39.,30.)) )
                          , 1.5*distance(uv,vec2(48.,42.)) ), distance(uv,vec2(90.,32.)) ), 0.8*distance(uv,vec2(98.,50.)) ), 1.15*distance(uv,vec2(120.,44.)) );;
            d *= (1.-0.8*(sat(hash(uv.x+uv.y)-0.6)+sat(huvx-0.6)));
            col = mix( col,COL(93.,77.,50.), sat((7.-d)/8.) );
            col = mix( col, vec3(0.), pow(sat((5.-d)/6.),1.5) );
        }
        else if( material == 6 ) {
            uv = mod(uv, vec2(64.));
            col = mix( COL(147.,126.,108.), COL(175.,152.,134.), sat( 1.5*(huv+hash(uv.x-uv.y)-0.95-uv.y/128.)) );
            col = mix( col, COL(175.,152.,134.), sat( 1.5*(huv+hash(uv.x-uv.y*1.1+5.)-1.8+uv.y/64.)) );
            col = mix( col, COL(130.,133.,108.), sat( 10.*(huv+hash(uv.x*1.1-uv.y+3.)-1.25)) );
            col = mix( col, mix( COL(118.,125.,99.), COL(130.,133.,108.), 1.-huv), sat(5.*(huv-1.5+uv.y/64.)) );
            col = mix( col, COL(129.,111.,91.), sat( onLineX(uv,0.)+onLineY(uv,63.) ) );
            col *= sat(0.92+huv);       
        } 
        else if( material == 7 ) {
            uv = mod(uv, vec2(64.)); 
            float h = hash(3.*uv.x+uv.y);
            col = mix( COL(136.,114.,95.), COL(143.,122.,104.), sat(4.*h*huv) );
            col = mix( col, COL(129.,111.,91.), sat(h-0.5) );   
            col *= 1.+0.05*sat( 0.3+mod(uv.x,2.)*cos(uv.y*0.2)*huv );
            col = mix( col, COL(175.,126.,89.), sat( 2.*(hash(floor(uv*0.125))+huv-1.5) ) );
            vec3 ncol = mix( col, COL(114.,94.,78.), sat( 
                (0.4*huv+0.4)*onRectAA( mod(uv+vec2(0.,33.),vec2(64.)), vec2(6.5,0.5), vec2(36.5,58.5) )
                             -onRectAA( mod(uv+vec2(0.,33.),vec2(64.)), vec2(9.5,3.5), vec2(33.5,55.5) ) ));
            ncol = mix( ncol, COL(114.,94.,78.), sat( (0.6*huv+0.3)*onRectAA( mod(uv+vec2(0.,5.),vec2(64.)), vec2(33.5,0.5), vec2(59.5,60.5) ) ));
            ncol = mix( ncol, col, sat(               0.8*onRectAA( mod(uv+vec2(0.,5.),vec2(64.)), vec2(35.5,2.5), vec2(57.5,58.5) ) ));
            ncol = mix( ncol, COL(121.,103.,81.), sat( (0.8*huv+0.9)*onRectAA( mod(uv+vec2(0.,53.),vec2(64.)), vec2(18.5,0.5), vec2(41.5,22.5) ) ));
            ncol = mix( ncol, col, sat(               onRectAA( mod(uv+vec2(0.,53.),vec2(64.)), vec2(19.5,1.5), vec2(40.5,21.5) ) ));
            ncol = mix( ncol, COL(114.,94.,78. ), sat( (0.8*huv+0.6)*onRectAA( mod(uv+vec2(8.,46.),vec2(64.)), vec2(0.5,0.5), vec2(20.5,36.5) ) ));
            col  = mix( ncol, col, sat(               onRectAA( mod(uv+vec2(8.,46.),vec2(64.)), vec2(1.5,1.5), vec2(19.5,35.5) ) ));
        } else  {
            col = vec3(0.5);
        }
    }

    struct lineDef { vec2 a; vec2 b; float h; float l; int m; };

    vec3 castRay( const vec3 ro, const vec3 rd ) {
        float dist = 999999., curdist; vec2 uv, curuv;
        vec3 col = vec3( 0. ); float lightning = 128.; int mat = 0;

        // DYNAMIC WALL RENDERER
        for( int i=0; i<80; i++ ) {
            if (i >= u_numLines) break;

            vec2 a = u_lineCoords[i].xy;
            vec2 b = u_lineCoords[i].zw;
            int m = int(u_lineProps[i].x);
            float l = u_lineProps[i].y;
            float h = 128.0; 
            
            if( intersectWall(ro, rd, a, b, h, dist, uv) || 
                intersectWall(ro, rd, b*vec2(-1.,1.), a*vec2(-1.,1.), h, dist, uv) ) {
                mat = m;
                lightning = l * (1.-0.2*abs( normalize( (a-b).yx ).y ));
            }
        }
        
        // Render Floor/Ceiling (Simplified for generic maps)
        if( mat == 5 ) { 
            vec3 intersection = ro + rd*dist;
            if( intersection.z > -0.1 ) {
                uv = -intersection.xy+vec2(64.,0.);
                lightning = 0.8*max(128., min(208., 248.-20.*floor(abs(intersection.x)/32.)));
            }
            uv *= 0.5;
        }
        
        if( intersectFloor(ro, rd, 264., dist, uv ) ) {
            mat = 1;
            lightning =128.;
            float c1=320., c2=196.;
            // Modified loop to fix WebGL 1.0 restrictions (no i-- or >=0)
            for( int k=0; k<5; k++ ) {
                int i = 4 - k;
                if( abs(uv.x)*(c1/c2)-uv.y < c1 ) {
                    lightning = float(208-i*16);
                }
                c1-=64.; c2-=32.;
            }
        }
        if( intersectFloor(ro, rd, 8., dist, uv ) ) {
            mat = 7;
            lightning =128.;
        }       
        float c1=64., c2=64., c3=48.;
        for( int i=0; i<5; i++ ) {
            curdist = dist;
            if( intersectFloor(ro, rd, c3, curdist, curuv ) && abs(curuv.x)*(c1/c2)-curuv.y < c1 ) {
                uv = curuv;
                mat = 7;
                dist = curdist;
                lightning = float(208-i*16);
            }
            c3-=8.; c1+=64.; c2+=32.;
        }
        
        curdist = dist;
        if( (intersectFloor(ro, rd, 56., curdist, curuv ) || intersectFloor(ro, rd, 128., curdist, curuv ) ) && curuv.y > 0. ) {
            dist = curdist;
            uv = curuv;
            mat = rd.y>0.?0:6;
            lightning = 224.;
        }
        
        getMaterialColor( mat, uv, col );

        col *= 0.3*pow(2.*lightning/255., 2.5)*sat( 1.-curdist/2000. ); 
        col = floor((col)*64.+vec3(0.5))/64.;
        return col;
    }

    void main() {
        vec2 q = gl_FragCoord.xy / u_resolution.xy;
        vec2 p = -1.0 + 2.0 * q;
        p.x *= u_resolution.x / u_resolution.y;

        vec3 ro = u_camPos; 
        ro.y = 64.0; // Fixed eye level height
        
        // Calculate the vector looking forward based on our rotation yaw
        vec2 dir = vec2(sin(u_camYaw), cos(u_camYaw));
        vec3 rdcenter = vec3(dir.x, 0.0, dir.y);
        
        // Construct standard look matrix
        vec3 uu = normalize(cross(vec3(0.0, 1.0, 0.0), rdcenter));
        vec3 vv = normalize(cross(rdcenter, uu));
        vec3 rd = normalize(p.x * uu + p.y * vv + 1.25 * rdcenter);
        
        vec3 col = castRay(ro, rd);
        gl_FragColor = vec4(col, 1.0);
    }
)";

GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    return shader;
}

// WASM EXPORTS FOR REACT 
extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void update_input(int f, int b, int l, int r, int tl, int tr) {
        in_f = f; in_b = b; in_l = l; in_r = r; in_tl = tl; in_tr = tr;
    }
    
    EMSCRIPTEN_KEEPALIVE
    void add_cam_yaw(float amount) {
        g_cam_yaw += amount;
    }

    EMSCRIPTEN_KEEPALIVE
    void clear_map() {
        g_num_lines = 0;
    }

    EMSCRIPTEN_KEEPALIVE
    void set_player_pos(float x, float z) {
        g_cam_x = x;
        g_cam_z = z;
    }

    EMSCRIPTEN_KEEPALIVE
    void add_wall(float x1, float y1, float x2, float y2, int material, float lighting) {
        if (g_num_lines < MAX_LINES) {
            int c_idx = g_num_lines * 4;
            g_line_coords[c_idx] = x1;     g_line_coords[c_idx+1] = y1;
            g_line_coords[c_idx+2] = x2;   g_line_coords[c_idx+3] = y2;
            
            int p_idx = g_num_lines * 2;
            g_line_props[p_idx] = (float)material;
            g_line_props[p_idx+1] = lighting;
            
            g_num_lines++;
        }
    }
}

void render_frame() {
    g_time += 0.016f; 
    float speed = 4.0f; 
    float rot_speed = 0.04f;

    if (in_tl) g_cam_yaw -= rot_speed;
    if (in_tr) g_cam_yaw += rot_speed;

    float dir_x = sin(g_cam_yaw);
    float dir_z = cos(g_cam_yaw);
    float right_x = cos(g_cam_yaw);
    float right_z = -sin(g_cam_yaw);

    if (in_f) { g_cam_x += dir_x * speed; g_cam_z += dir_z * speed; }
    if (in_b) { g_cam_x -= dir_x * speed; g_cam_z -= dir_z * speed; }
    if (in_l) { g_cam_x -= right_x * speed; g_cam_z -= right_z * speed; }
    if (in_r) { g_cam_x += right_x * speed; g_cam_z += right_z * speed; }

    double css_w, css_h;
    emscripten_get_element_css_size("#canvas", &css_w, &css_h);
    static double canvas_w = 0, canvas_h = 0;
    if (canvas_w != css_w || canvas_h != css_h) {
        canvas_w = css_w; canvas_h = css_h;
        emscripten_set_canvas_element_size("#canvas", canvas_w, canvas_h);
        glViewport(0, 0, canvas_w, canvas_h);
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(g_program);

    glUniform2f(glGetUniformLocation(g_program, "u_resolution"), (float)canvas_w, (float)canvas_h);
    glUniform1f(glGetUniformLocation(g_program, "u_time"), g_time);
    glUniform3f(glGetUniformLocation(g_program, "u_camPos"), g_cam_x, g_cam_y, g_cam_z);
    glUniform1f(glGetUniformLocation(g_program, "u_camYaw"), g_cam_yaw);

    // Upload the Map arrays to the GPU
    glUniform1i(glGetUniformLocation(g_program, "u_numLines"), g_num_lines);
    if (g_num_lines > 0) {
        glUniform4fv(glGetUniformLocation(g_program, "u_lineCoords"), g_num_lines, g_line_coords);
        glUniform2fv(glGetUniformLocation(g_program, "u_lineProps"), g_num_lines, g_line_props);
    }

    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    GLint posAttrib = glGetAttribLocation(g_program, "a_position");
    glEnableVertexAttribArray(posAttrib);
    glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

int main() {
    EmscriptenWebGLContextAttributes attr;
    emscripten_webgl_init_context_attributes(&attr);
    attr.alpha = 0; 

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attr);
    emscripten_webgl_make_context_current(ctx);

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_source);
    g_program = glCreateProgram();
    glAttachShader(g_program, vs);
    glAttachShader(g_program, fs);
    glLinkProgram(g_program);

    float vertices[] = {
        -1.0f,  1.0f,   -1.0f, -1.0f,    1.0f, -1.0f,
        -1.0f,  1.0f,    1.0f, -1.0f,    1.0f,  1.0f
    };
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    emscripten_set_main_loop(render_frame, 0, 1);
    return 0;
}