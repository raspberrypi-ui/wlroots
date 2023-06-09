precision mediump float;
varying vec2 v_texcoord;
uniform sampler2D tex;
uniform float alpha;

void main() {
	gl_FragColor = vec4(texture2D(tex, v_texcoord).rgb, 1.0) * alpha;
}
