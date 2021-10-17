
uniform float iTime;
uniform vec2 iResolution;
uniform sampler2D iChannel0;
uniform sampler2D iChannel1;

void main() {
    vec2 UV = gl_FragCoord.xy / iResolution.xy;

    gl_FragColor = texture(iChannel0, UV);
    //gl_FragColor = vec4(1.0);
}