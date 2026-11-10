// The MIT License
// Copyright (c) 2018 Inigo Quilez
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


float dot2( vec2 v ) { return dot(v,v); }
float cro( vec2 a, vec2 b ) { return a.x*b.y-a.y*b.x; }
float cos_acos_3( float x ) { x=sqrt(0.5+0.5*x); return x*(x*(x*(x*-0.008972+0.039071)-0.107074)+0.576975)+0.5; } // https://www.shadertoy.com/view/WltSD7

// This method provides just an approximation, and is only usable in
// the very close neighborhood of the curve. Taken and adapted from
// http://research.microsoft.com/en-us/um/people/hoppe/ravg.pdf
float sdBezier( vec2 p, vec2 v0, vec2 v1, vec2 v2, out float t)
{
	vec2 i = v0 - v2;
    vec2 j = v2 - v1;
    vec2 k = v1 - v0;
    vec2 w = j-k; 

	v0-= p; v1-= p; v2-= p;
    
	float x = cro(v0, v2);
    float y = cro(v1, v0);
    float z = cro(v2, v1);

	vec2 s = 2.0*(y*j+z*k)-x*i;

    float r =  (y*z-x*x*0.25)/dot2(s);
    t = clamp( (0.5*x+y+r*dot(s,w))/(x+y+z),0.0,1.0);
    
    vec2 d = v0+t*(k+k+t*w);
	return length(d);
}

// The exact distance from p to the quadratic Bezier v0, v1, v2: the nearest point solved as
// a cubic (one real root or three), so it holds anywhere, not only near the curve. A quadratic
// that bends less than a quarter of a pixel off its chord is measured as the straight segment,
// where the cubic's numbers would run out of precision. t is the nearest point's parameter.
float sdBezierExact( vec2 p, vec2 v0, vec2 v1, vec2 v2, out float t )
{
    vec2 a = v1 - v0;
    vec2 b = v0 - 2.0*v1 + v2;
    vec2 c = a * 2.0;
    vec2 d = v0 - p;

    // The farthest a quadratic gets from its chord is a quarter of |b|.
    if (dot(b, b) < 1.0)
    {
        vec2 chord = v2 - v0;
        float l2 = dot(chord, chord);
        t = l2 > 0.0 ? clamp(dot(p - v0, chord) / l2, 0.0, 1.0) : 0.0;
        return length(p - (v0 + chord * t));
    }

    float kk = 1.0 / dot(b, b);
    float kx = kk * dot(a, b);
    float ky = kk * (2.0*dot(a, a) + dot(d, b)) / 3.0;
    float kz = kk * dot(d, a);

    float pp = ky - kx*kx;
    float q  = kx*(2.0*kx*kx - 3.0*ky) + kz;
    float h  = q*q + 4.0*pp*pp*pp;
    if (h >= 0.0)
    {
        // One root.
        h = sqrt(h);
        vec2 x  = (vec2(h, -h) - q) / 2.0;
        vec2 uv = sign(x) * pow(abs(x), vec2(1.0/3.0));
        t = clamp(uv.x + uv.y - kx, 0.0, 1.0);
        return length(d + (c + b*t)*t);
    }
    // Three: the two that can be nearest.
    float z = sqrt(-pp);
    float v = acos(clamp(q / (pp*z*2.0), -1.0, 1.0)) / 3.0;
    float m = cos(v);
    float n = sin(v) * 1.732050808;
    vec2  ts = clamp(vec2(m + m, -n - m) * z - kx, 0.0, 1.0);
    float d1 = length(d + (c + b*ts.x)*ts.x);
    float d2 = length(d + (c + b*ts.y)*ts.y);
    t = d1 < d2 ? ts.x : ts.y;
    return min(d1, d2);
}
