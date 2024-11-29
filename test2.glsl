void main() {
    vec2 UV = gl_FragCoord.xy / iResolution.xy;

    gl_FragColor = texture2D(iChannel0, UV);
    //gl_FragColor = vec4(1.0);
}
