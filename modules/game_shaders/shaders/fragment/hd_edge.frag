// hd_edge.frag - Escalado dirigido por bordes para pixel art (familia xBR/Scale2x).
//
// No inventa detalle: reconstruye las diagonales que el pixel art insinua, de modo
// que las escaleras de pixeles se ven como lineas limpias. Es el mismo principio que
// usan los emuladores retro. A diferencia de un desenfoque, respeta los bordes duros.
//
// Aplicado sobre el framebuffer del mapa, asi que cuesta lo mismo sea cual sea el
// numero de sprites en pantalla.

uniform sampler2D u_Tex0;
uniform vec2 u_Resolution;
uniform float u_Time;
varying vec2 v_TexCoord;

// Pesos de luminancia perceptual: el ojo distingue mucho mejor el verde.
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

// Cuanta diferencia hace falta para considerar que dos pixeles son "distintos".
// Mas bajo = detecta mas bordes (y suaviza mas). Mas alto = mas conservador.
const float THRESHOLD = 0.10;

// Cuanto se mezcla en las esquinas detectadas. 0 = sin efecto, 1 = maximo.
const float STRENGTH = 0.55;

float colorDist(vec3 a, vec3 b) {
    return dot(abs(a - b), LUMA);
}

bool similar(vec3 a, vec3 b) {
    return colorDist(a, b) < THRESHOLD;
}

void main() {
    vec2 texel = 1.0 / u_Resolution;
    vec2 uv = v_TexCoord;

    // Vecindario de 3x3 alrededor del pixel actual (E).
    //   A B C
    //   D E F
    //   G H I
    vec3 B = texture2D(u_Tex0, uv + texel * vec2( 0.0, -1.0)).rgb;
    vec3 D = texture2D(u_Tex0, uv + texel * vec2(-1.0,  0.0)).rgb;
    vec4 Efull = texture2D(u_Tex0, uv);
    vec3 E = Efull.rgb;
    vec3 F = texture2D(u_Tex0, uv + texel * vec2( 1.0,  0.0)).rgb;
    vec3 H = texture2D(u_Tex0, uv + texel * vec2( 0.0,  1.0)).rgb;

    // Posicion dentro del texel: dice en que cuadrante del pixel estamos dibujando.
    vec2 f = fract(uv * u_Resolution);

    vec3 result = E;

    // Regla de Scale2x: si dos vecinos opuestos en cruz se parecen entre si pero
    // difieren del perpendicular, hay una diagonal implicita. Se mezcla hacia ese
    // color solo en la esquina correspondiente.
    bool bdEdge = similar(B, D) && !similar(B, H) && !similar(D, F);
    bool bfEdge = similar(B, F) && !similar(B, H) && !similar(D, F);
    bool hdEdge = similar(H, D) && !similar(B, H) && !similar(D, F);
    bool hfEdge = similar(H, F) && !similar(B, H) && !similar(D, F);

    if (bdEdge && f.x < 0.5 && f.y < 0.5) {
        result = mix(E, B, STRENGTH);
    } else if (bfEdge && f.x >= 0.5 && f.y < 0.5) {
        result = mix(E, B, STRENGTH);
    } else if (hdEdge && f.x < 0.5 && f.y >= 0.5) {
        result = mix(E, H, STRENGTH);
    } else if (hfEdge && f.x >= 0.5 && f.y >= 0.5) {
        result = mix(E, H, STRENGTH);
    }

    gl_FragColor = vec4(result, Efull.a);
}
