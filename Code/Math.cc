vec4
row(const mat4& m, int j)
{
   return vec4{m[0][j], m[1][j], m[2][j], m[3][j]};
}


float
lerp(float a, float b, float interp)
{
   float r = a * (1.0f - interp) + b * interp;
   return r;
}

vec3
lerp(vec3 a, vec3 b, float interp)
{
   vec3 r = a * (1.0f - interp) + b * interp;
   return r;
}

float
clamp(float v, float min, float max) {
   if (v < min) { return min; }
   if (v > max) { return max; }
   return v;
}

float
signedArea(vec2 a, vec2 b, vec2 c)
{
   float area = (a.x - c.x) * (b.y - c.y) - (b.x - c.x) * (a.y - c.y);
   return area;
}

float
sign(float x)
{
   return x >= 0? 1.0f : -1.0f;
}

float
dot(const vec2& a, const vec2& b)
{
   float r = a.x * b.x + a.y * b.y;
   return r;
}

float
dot(const vec3& a, const vec3& b)
{
   float r = 0.0f;
   for (sz i = 0; i < 3; ++i) {
      r += a[i] * b[i];
   }
   return r;
}

float
dot(const vec4& a, const vec4& b)
{
   float r = 0.0f;
   for (sz i = 0; i < 4; ++i) {
      r += a[i] * b[i];
   }
   return r;
}

float
length(vec2 v)
{
   return sqrtf(dot(v, v));
}

float
length(vec3 v)
{
   return sqrtf(dot(v, v));
}

float
length(vec4 v)
{
   return sqrtf(dot(v, v));
}

// ==== mat4 operators
vec4 operator*(const mat4& m, const vec4& v)
{
   vec4 r = {
      dot(row(m, 0), v),
      dot(row(m, 1), v),
      dot(row(m, 2), v),
      dot(row(m, 3), v)
   };
   return r;
}

mat4 operator*(const mat4& a, const mat4& b)
{
   mat4 r;
   for (int coli = 0; coli < 4; ++coli) {
      for (int rowi = 0; rowi < 4; ++rowi) {
         r[coli][rowi] = dot(row(a, rowi), b[coli]);
      }
   }
   return r;
}

vec4
toVec4(const vec3& v, float w)
{
   vec4 r = {
      v.x,
      v.y,
      v.z,
      w
   };
   return r;
}

mat4
mat4Identity()
{
   mat4 m = {};
   m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
   return m;
}

mat4
mat4Scale(float scale)
{
   mat4 m = {};
   m[0][0] = m[1][1] = m[2][2] = scale;
   m[3][3] = 1.0f;
   return m;
}

mat4
mat4Orientation(vec3 pos, vec3 dir, vec3 up)
{
   vec3 front = normalizedOrZero(dir);
   vec3 right = normalizedOrZero(cross(up, front));
   //up = normalizedOrZero(cross(front, right));

   mat4 m = mat4Identity();
   m.cols[3].xyz = pos;
   m.cols[2].xyz = front;
   m.cols[1].xyz = up;
   m.cols[0].xyz = right;
   return m;
}

mat4
mat4Euler(const float roll, const float pitch, const float yaw)
{
   float cr = cos(roll);
   float sr = sin(roll);

   float cp = cos(pitch);
   float sp = sin(pitch);

   float ch = cos(yaw);
   float sh = sin(yaw);

   // mat4 m = {
   //    cr*ch - sr*sp*sh,    -sr*cp,  cr*sh + sr*sp*ch,    0,
   //    sr*ch + cr*sp*sh,    cr*cp,   sr*sh - cr*sp*ch,    0,
   //    -cp*sh,              sp,      cp*ch,               0,
   //    0,                  0,         0,                  1,
   // };


   mat4 m = {
      cr*ch - sr*sp*sh,    sr*ch + cr*sp*sh,    -cp*sh,  0,
      -sr*cp,              cr*cp,               sp,      0,
      cr*sh + sr*sp*ch,    sr*sh - cr*sp*ch,    cp*ch,   0,
      0,                   0,                   0,       1,
   };

   return m;
}

bool
almostEquals(const float a, const float b)
{
   float d = a-b;
   return Abs(d) < 1.0e-6;
}

vec3
cross(const vec3& a, const vec3& b)
{
   vec3 c = {
      a.y*b.z - a.z*b.y,
      a.z*b.x - a.x*b.z,
      a.x*b.y - a.y*b.x,
   };
   return c;
}

float
norm(const vec2& v)
{
   return sqrt(v.x*v.x + v.y*v.y);
}

float
norm(const vec3& v)
{
   return sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
}

vec2
normalized(const vec2& v)
{
   Assert(norm(v) != 0.0f);
   vec2 n = v / norm(v);
   return n;
}

vec3
normalized(const vec3& v)
{
   Assert(norm(v) != 0.0f);
   vec3 n = v / norm(v);
   return n;
}

vec2
normalizedOrZero(const vec2& v)
{
   vec2 n = v;
   float l = norm(v);
   if (l != 0.0f) {
      n /= l;
   }

   return n;
}

vec3
normalizedOrZero(const vec3& v)
{
   vec3 n = v;
   float l = norm(v);
   if (l != 0.0f) {
      n /= l;
   }

   return n;
}

vec4
saturate(const vec4& v)
{
   vec4 r = {
      clamp(v.x, 0, 1),
      clamp(v.y, 0, 1),
      clamp(v.z, 0, 1),
      clamp(v.w, 0, 1),
   };
   return r;
}

mat4
mat4Transpose(const mat4& m)
{
   mat4 r;
   for (sz i = 0; i < 4; ++i) {
      r[i] = row(m, i);
   }
   return r;
}

mat4
mat4Lookat(const vec3& eye, const vec3& lookat, const vec3& up)
{
   Assert(almostEquals(norm(up), 1.0f));

   mat4 m = {};

   float n = norm(lookat - eye);

   vec3 bz = (lookat - eye) / n;
   vec3 bx = cross(up, bz);

   // In the case that the up vector is colinear with the view direction, default to an orthogonal up vector...
   if (length(bx) == 0) {
      vec3 upFixed = normalized(cross(up, up + vec3{1,1,1}));
      bx = cross(upFixed, bz);
   }
   vec3 by = cross(bz, bx);

   m = mat4Identity();

   m[0] = vec4{bx.x, by.x, bz.x, 0.0f};
   m[1] = vec4{bx.y, by.y, bz.y, 0.0f};
   m[2] = vec4{bx.z, by.z, bz.z, 0.0f};

   // Translation
   m[3].x = -dot(eye, bx);
   m[3].y = -dot(eye, by);
   m[3].z = -dot(eye, bz);

   m[3][3] = 1.0f;

   return m;
}

mat4
mat4Persp(const Camera* c, float aspect)
{
   // Unit frustrum [-1,1]^2 in XY plane with Z in  [0,1]
   float overTan = 1 / (tan(c->fov / 2.0f));
   mat4 persp = {};
   persp[0][0] = overTan / aspect;
   persp[1][1] = overTan;
   persp[2][2] = c->far / (c->far - c->near);
   persp[2][3] = 1;
   persp[3][2] = -(c->far * c->near) / (c->far - c->near);

   // z*(f - n) / (f*n)
   return persp;
}

mat4
mat4Translate(f32 x, f32 y, f32 z)
{
   mat4 r = mat4Identity();
   r[3][0] = x;
   r[3][1] = y;
   r[3][2] = z;
   return r;
}

mat4
mat4Translate(vec3 p)
{
   return mat4Translate(p.x, p.y, p.z);
}


// float
// mat4Determinant(const mat4& m) {

// }

mat4
mat4Inverse(const mat4& m)
{
   mat4 r = {};

   // Thinking about this in row major...
   #define a11 m[0][0]
   #define a12 m[1][0]
   #define a13 m[2][0]
   #define a14 m[3][0]
   #define a21 m[0][1]
   #define a22 m[1][1]
   #define a23 m[2][1]
   #define a24 m[3][1]
   #define a31 m[0][2]
   #define a32 m[1][2]
   #define a33 m[2][2]
   #define a34 m[3][2]
   #define a41 m[0][3]
   #define a42 m[1][3]
   #define a43 m[2][3]
   #define a44 m[3][3]

   // Determinants of cofactor matrices.
   float cf11 =    a22*a33*a44 + a23*a34*a42 + a24*a32*a43
                 - a24*a33*a42 - a23*a32*a44 - a22*a34*a43;
   float cf12 =  a21*a33*a44 + a23*a34*a41 + a24*a31*a43
               - a24*a33*a41 - a23*a31*a44 - a21*a34*a43;
   float cf13 =   a21*a32*a44 + a22*a34*a41 + a24*a31*a42
                - a24*a32*a41 - a22*a31*a44 - a21*a34*a42;
   float cf14 =   a21*a32*a43 + a22*a33*a41 + a23*a31*a42
                - a23*a32*a41 - a22*a31*a43 - a21*a33*a42;

   float cf21 = a12*a33*a44 + a13*a34*a42 + a14*a32*a43
               - a14*a33*a42 - a13*a32*a44 - a12*a34*a43;
   float cf22 = a11*a33*a44 + a13*a34*a41 + a14*a31*a43
               - a14*a33*a41 - a13*a31*a44 - a11*a34*a43;
   float cf23 = a11*a32*a44 + a12*a34*a41 + a14*a31*a42
               - a14*a32*a41 - a12*a31*a44 - a11*a34*a42;
   float cf24 = a11*a32*a43 + a12*a33*a41 + a13*a31*a42
               - a13*a32*a41 - a12*a31*a43 - a11*a33*a42;

   float cf31 = a12*a23*a44 + a13*a24*a42 + a14*a22*a43
               - a14*a23*a42 - a13*a22*a44 - a12*a24*a43;
   float cf32 = a11*a23*a44 + a13*a24*a41 + a14*a21*a43
               - a14*a23*a41 - a13*a21*a44 - a11*a24*a43;
   float cf33 = a11*a22*a44 + a12*a24*a41 + a14*a21*a42
               - a14*a22*a41 - a12*a21*a44 - a11*a24*a42;
   float cf34 = a11*a22*a43 + a12*a23*a41 + a13*a21*a42
               - a13*a22*a41 - a12*a21*a43 - a11*a23*a42;

   float cf41 = a12*a23*a34 + a13*a24*a32 + a14*a22*a33
               - a14*a23*a32 - a13*a22*a34 - a12*a24*a33;
   float cf42 = a11*a23*a34 + a13*a24*a31 + a14*a21*a33
               - a14*a23*a31 - a13*a21*a34 - a11*a24*a33;
   float cf43 = a11*a22*a34 + a12*a24*a31 + a14*a21*a32
               - a14*a22*a31 - a12*a21*a34 - a11*a24*a32;
   float cf44 = a11*a22*a33 + a12*a23*a31 + a13*a21*a32
               - a13*a22*a31 - a12*a21*a33 - a11*a23*a32;


   float det = a11 * cf11 - a21 * cf21 + a31 * cf31 - a41*cf41;

   Assert(det != 0);

   #undef a11
   #undef a12
   #undef a13
   #undef a14
   #undef a21
   #undef a22
   #undef a23
   #undef a24
   #undef a31
   #undef a32
   #undef a33
   #undef a32
   #undef a41
   #undef a42
   #undef a43
   #undef a44

   r[0][0] = cf11;
   r[1][0] = -cf21;
   r[2][0] = cf31;
   r[3][0] = -cf41;

   r[0][1] = -cf12;
   r[1][1] = cf22;
   r[2][1] = -cf32;
   r[3][1] = cf42;

   r[0][2] = cf13;
   r[1][2] = -cf23;
   r[2][2] = cf33;
   r[3][2] = -cf43;

   r[0][3] = -cf14;
   r[1][3] = cf24;
   r[2][3] = -cf34;
   r[3][3] = cf44;

   for (int i = 0; i < 4; ++i){
      r[i] /= det;
   }

   return r;
}

vec4
Vec4(f32 x, f32 y, f32 z, f32 w)
{
   return vec4{x, y, z, w};
}

vec3
Vec3(f32 x, f32 y, f32 z)
{
   return vec3{x,y,z};
}

vec2
Vec2(f32 x, f32 y)
{
   return vec2{x,y};
}
