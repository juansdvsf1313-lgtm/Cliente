// hd_anime_sr.frag - Super-resolucion de imagen unica para arte tipo anime.
//
// Sigue el planteamiento de "Single-Image Super-Resolution for Anime-Style Art
// using Deep Convolutional Neural Networks" (waifu2x): en vez de interpolar a
// ciegas, primero se analiza la estructura local, luego se reconstruye siguiendo
// esa estructura, y al final se devuelve el detalle de alta frecuencia como
// residuo. Esa secuencia es la que hace que el resultado se vea dibujado y no
// emborronado.
//
// La red original son 7 capas convolucionales con pesos entrenados; eso no cabe
// en un fragment shader de una sola pasada (harian falta varias pasadas y una
// textura de pesos). Aqui cada bloque se sustituye por su equivalente analitico,
// que es determinista y no necesita entrenamiento:
//
//   capa 1     extraccion de rasgos -> tensor de estructura sobre la luminancia
//   capas 2-6  mapeo no lineal      -> nucleo anisotropico orientado al borde
//   capa 7     reconstruccion       -> residuo de alta frecuencia + anti-halo
//
// Y un paso extra propio del arte anime, que la red aprende sola y aqui va
// explicito: reforzar la linea de contorno oscura, que es lo primero que se
// pierde al ampliar sprites de 32x32.
//
// Se aplica sobre el framebuffer del mapa ya compuesto, asi que cuesta lo mismo
// haya un sprite o mil: 16 lecturas de textura por pixel de pantalla.

uniform sampler2D u_Tex0;
uniform mat3 u_TextureMatrix; // su diagonal es (1/ancho, 1/alto) de la textura
uniform vec2 u_Resolution;    // solo como respaldo si la matriz no llegara
varying vec2 v_TexCoord;

// Pesos de luminancia perceptual: el ojo distingue mucho mejor el verde.
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

// --- Ajustes ---------------------------------------------------------------
// Los presets del combo de Ctrl+Y son este mismo shader compilado con valores
// distintos: shaders.lua antepone los #define. Los de aqui son los que se usan
// si no llega ninguna definicion.

#ifndef SR_SIGMA_FLAT
#define SR_SIGMA_FLAT 0.62
#endif
#ifndef SR_SIGMA_ALONG
#define SR_SIGMA_ALONG 0.85
#endif
#ifndef SR_SIGMA_ACROSS
#define SR_SIGMA_ACROSS 0.28
#endif
#ifndef SR_SHARPNESS
#define SR_SHARPNESS 0.70
#endif
#ifndef SR_LINE_DARK
#define SR_LINE_DARK 0.25
#endif

// Ancho del nucleo donde NO hay borde: suelos, piedra, dithering. Es el que
// decide si esas zonas se ven borrosas o definidas, porque ahi la orientacion
// no aporta nada y el nucleo se queda isotropo. Bajarlo es lo que da sensacion
// de HD; subirlo tapa el ruido del dithering a costa de emborronar.
const float SIGMA_FLAT = SR_SIGMA_FLAT;

// Ancho del nucleo A LO LARGO del borde. Mas alto = diagonales mas continuas.
const float SIGMA_ALONG = SR_SIGMA_ALONG;

// Ancho del nucleo A TRAVES del borde. Mas bajo = borde mas duro, mas contraste.
const float SIGMA_ACROSS = SR_SIGMA_ACROSS;

// Fuerza del residuo de alta frecuencia. 0 = sin realce, 1 = muy marcado.
// Se puede subir sin miedo a halos porque despues viene el clamp ANTIRING.
const float SHARPNESS = SR_SHARPNESS;

// Refuerzo de las lineas de contorno oscuras. Subirlo marca mas el dibujo;
// pasarse ensucia las sombras y engorda los contornos.
const float LINE_DARK = SR_LINE_DARK;

// 1 = el pixel resultante nunca sale del rango de sus 4 vecinos (cero halos).
const float ANTIRING = 1.0;

void main()
{
    // Tamano real de la textura de origen. u_Resolution es la resolucion de
    // salida (la ventana), no la del mapa, asi que no sirve aqui: el mapa se
    // renderiza a (tiles+3)*32 pixeles y luego se estira al panel. Esa diferencia
    // es justamente el factor de ampliacion que hay que reconstruir.
    vec2 invSize = vec2(abs(u_TextureMatrix[0][0]), abs(u_TextureMatrix[1][1]));
    vec2 texSize = (invSize.x > 0.0 && invSize.y > 0.0) ? 1.0 / invSize : u_Resolution;
    vec2 texel = 1.0 / texSize;

    // Posicion continua sobre la rejilla de texels de origen.
    vec2 p = v_TexCoord * texSize - 0.5;
    vec2 corner = floor(p);
    vec2 f = p - corner;              // 0..1 dentro del texel de referencia
    vec2 base = (corner + 0.5) * texel;

    // Vecindario de 4x4 (de -1 a +2). Se muestrea en el centro exacto de cada
    // texel, asi el resultado es identico con filtrado nearest o linear.
    vec4 c[16];
    float l[16];
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            vec2 o = vec2(float(x) - 1.0, float(y) - 1.0);
            vec4 s = texture2D(u_Tex0, base + o * texel);
            c[y * 4 + x] = s;
            l[y * 4 + x] = dot(s.rgb, LUMA);
        }
    }

    // --- Capa 1: extraccion de rasgos ---------------------------------------
    // Gradientes por diferencias centrales en los 4 texels interiores. El tensor
    // de estructura resume los cuatro en una sola direccion dominante mas una
    // medida de cuanto se puede confiar en ella.
    vec2 g5  = vec2(l[6]  - l[4], l[9]  - l[1]);
    vec2 g6  = vec2(l[7]  - l[5], l[10] - l[2]);
    vec2 g9  = vec2(l[10] - l[8], l[13] - l[5]);
    vec2 g10 = vec2(l[11] - l[9], l[14] - l[6]);

    float Jxx = g5.x * g5.x + g6.x * g6.x + g9.x * g9.x + g10.x * g10.x;
    float Jyy = g5.y * g5.y + g6.y * g6.y + g9.y * g9.y + g10.y * g10.y;
    float Jxy = g5.x * g5.y + g6.x * g6.y + g9.x * g9.y + g10.x * g10.y;

    float tr = Jxx + Jyy;
    float det = Jxx * Jyy - Jxy * Jxy;
    float disc = sqrt(max(tr * tr * 0.25 - det, 0.0));
    float lam1 = tr * 0.5 + disc;   // energia a traves del borde
    float lam2 = tr * 0.5 - disc;   // energia a lo largo del borde

    // 0 = zona plana o ruido sin direccion clara, 1 = borde limpio y orientado.
    // Un borde de verdad ademas necesita contraste, no solo direccion; por eso se
    // atenua con lam1: si no, el shader inventa diagonales dentro de un degradado.
    float coherence = (lam1 - lam2) / (lam1 + lam2 + 1e-6);
    coherence *= smoothstep(0.0008, 0.02, lam1);

    // Autovector de lam1 = normal del borde; la tangente es la perpendicular.
    vec2 nrm = vec2(Jxy, lam1 - Jxx);
    float nlen = length(nrm);
    nrm = (nlen > 1e-6) ? nrm / nlen : vec2(1.0, 0.0);
    vec2 tng = vec2(-nrm.y, nrm.x);

    // --- Capas 2-6: mapeo no lineal -----------------------------------------
    // Nucleo gaussiano estirado a lo largo del borde y apretado a traves de el.
    // Asi la escalera de pixeles de una diagonal se promedia consigo misma en vez
    // de mezclarse con lo que hay al otro lado del contorno. Donde no hay borde
    // (coherence 0) degenera en un nucleo isotropo, o sea interpolacion normal.
    float sa = mix(SIGMA_FLAT, SIGMA_ALONG, coherence);
    float sn = mix(SIGMA_FLAT, SIGMA_ACROSS, coherence);
    float invA = 0.5 / (sa * sa);
    float invN = 0.5 / (sn * sn);

    vec4 acc = vec4(0.0);
    float accW = 0.0;
    vec4 wide = vec4(0.0);   // la misma vecindad pero desenfocada, para el residuo
    float wideW = 0.0;

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            vec2 d = vec2(float(x) - 1.0, float(y) - 1.0) - f;

            float da = dot(d, tng);
            float dn = dot(d, nrm);
            float w = exp(-(da * da * invA + dn * dn * invN));
            acc += c[y * 4 + x] * w;
            accW += w;

            float ww = exp(-dot(d, d) * 0.35);
            wide += c[y * 4 + x] * ww;
            wideW += ww;
        }
    }

    vec4 sr = acc / max(accW, 1e-5);
    wide /= max(wideW, 1e-5);

    // --- Capa 7: reconstruccion residual ------------------------------------
    // waifu2x aprende el residuo en vez de la imagen entera. Aqui el residuo se
    // calcula: es lo que el nucleo acaba de suavizar, y se devuelve pesado. En
    // zonas planas se devuelve menos, para no resucitar el ruido del dithering.
    vec4 residual = sr - wide;
    sr += residual * (SHARPNESS * mix(0.35, 1.0, coherence));

    // Sin este clamp el residuo genera el halo claro/oscuro tipico del enfoque
    // mal hecho. Limitar al rango de los 4 vecinos reales lo elimina de raiz.
    vec4 lo = min(min(c[5], c[6]), min(c[9], c[10]));
    vec4 hi = max(max(c[5], c[6]), max(c[9], c[10]));
    sr = mix(sr, clamp(sr, lo, hi), ANTIRING);

    // --- Contornos ----------------------------------------------------------
    // En arte anime la linea oscura define la forma. Al ampliar se adelgaza y se
    // vuelve gris. Se empuja de vuelta hacia el vecino mas oscuro, pero solo
    // donde hay un borde real, con contraste, y solo en el lado oscuro del borde.
    float loL = dot(lo.rgb, LUMA);
    float hiL = dot(hi.rgb, LUMA);
    float contrast = hiL - loL;
    float side = (dot(sr.rgb, LUMA) - loL) / max(contrast, 1e-4);
    float dark = LINE_DARK * coherence
               * smoothstep(0.06, 0.28, contrast)
               * (1.0 - smoothstep(0.0, 0.55, side));
    sr.rgb = mix(sr.rgb, lo.rgb, clamp(dark, 0.0, 1.0));

    gl_FragColor = clamp(sr, 0.0, 1.0);
}
