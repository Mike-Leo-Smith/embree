// ======================================================================== //
// Copyright 2009-2015 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "quadv.h"
#include "triangle_intersector_moeller.h"

/*! This intersector implements a modified version of the Moeller
 *  Trumbore intersector from the paper "Fast, Minimum Storage
 *  Ray-Triangle Intersection". In contrast to the paper we
 *  precalculate some factors and factor the calculations differently
 *  to allow precalculating the cross product e1 x e2. The resulting
 *  algorithm is similar to the fastest one of the paper "Optimizing
 *  Ray-Triangle Intersection via Automated Search". */

namespace embree
{
  namespace isa
  {
    template<int M>
      struct QuadHitM
      {
        __forceinline QuadHitM(const vfloat<M>& U, 
                               const vfloat<M>& V, 
                               const vfloat<M>& T, 
                               const vfloat<M>& absDen, 
                               const Vec3<vfloat<M>>& Ng, 
                               const vbool<M>& flags)
          : U(U), V(V), T(T), absDen(absDen), flags(flags), tri_Ng(Ng) {}
      
        __forceinline void finalize() 
        {
          const vfloat<M> rcpAbsDen = rcp(absDen);
          vt = T * rcpAbsDen;
          const vfloat<M> u = U * rcpAbsDen;
          const vfloat<M> v = V * rcpAbsDen;
          const vfloat<M> u1 = vfloat<M>(1.0f) - u;
          const vfloat<M> v1 = vfloat<M>(1.0f) - v;
#if !defined(__AVX__)
          vu = select(flags,u1,u); 
          vv = select(flags,v1,v);
          vNg = Vec3<vfloat<M>>(tri_Ng.x,tri_Ng.y,tri_Ng.z);
#else
          const vfloat<M> flip = select(flags,vfloat<M>(-1.0f),vfloat<M>(1.0f));
          vv = select(flags,u1,v);
          vu = select(flags,v1,u);
          vNg = Vec3<vfloat<M>>(flip*tri_Ng.x,flip*tri_Ng.y,flip*tri_Ng.z);
#endif
        }

        __forceinline Vec2f uv(const size_t i) 
        { 
          const float u = vu[i];
          const float v = vv[i];
          return Vec2f(u,v);
        }

        __forceinline float   t(const size_t i) { return vt[i]; }
        __forceinline Vec3fa Ng(const size_t i) { return Vec3fa(vNg.x[i],vNg.y[i],vNg.z[i]); }
      
      private:
        const vfloat<M> U;
        const vfloat<M> V;
        const vfloat<M> T;
        const vfloat<M> absDen;
        const vbool<M> flags;
        const Vec3<vfloat<M>> tri_Ng;
      
      public:
        vfloat<M> vu;
        vfloat<M> vv;
        vfloat<M> vt;
        Vec3<vfloat<M>> vNg;
      };


    template<int K>
      struct QuadHitK
      {
        __forceinline QuadHitK(const vfloat<K>& U, 
                               const vfloat<K>& V, 
                               const vfloat<K>& T, 
                               const vfloat<K>& absDen, 
                               const Vec3<vfloat<K>>& Ng, 
                               const vbool<K>& flags)
          : U(U), V(V), T(T), absDen(absDen), flags(flags), tri_Ng(Ng) {}
      
        __forceinline std::tuple<vfloat<K>,vfloat<K>,vfloat<K>,Vec3<vfloat<K>>> operator() () const
        {
          const vfloat<K> rcpAbsDen = rcp(absDen);
          const vfloat<K> t = T * rcpAbsDen;
          const vfloat<K> u0 = U * rcpAbsDen;
          const vfloat<K> v0 = V * rcpAbsDen;
          const vfloat<K> u1 = vfloat<K>(1.0f) - u0;
          const vfloat<K> v1 = vfloat<K>(1.0f) - v0;
          const vfloat<K> u = select(flags,u1,u0); 
          const vfloat<K> v = select(flags,v1,v0);
          const Vec3<vfloat<K>> Ng(tri_Ng.x,tri_Ng.y,tri_Ng.z);
          return std::make_tuple(u,v,t,Ng);
        }

      private:
        const vfloat<K> U;
        const vfloat<K> V;
        const vfloat<K> T;
        const vfloat<K> absDen;
        const vbool<K> flags;
        const Vec3<vfloat<K>> tri_Ng;      
      };


    /* ----------------------------- */
    /* -- single ray intersectors -- */
    /* ----------------------------- */

    template<int M>
      struct MoellerTrumboreIntersectorQuad1
      {
        __forceinline MoellerTrumboreIntersectorQuad1(const Ray& ray, const void* ptr) {}

        template<typename Epilog>
        __forceinline bool intersect(Ray& ray, 
                                     const Vec3<vfloat<M>>& tri_v0, 
                                     const Vec3<vfloat<M>>& tri_e1, 
                                     const Vec3<vfloat<M>>& tri_e2, 
                                     const Vec3<vfloat<M>>& tri_Ng,
                                     const vbool<M>& flags,
                                     const Epilog& epilog) const
        {
          /* calculate denominator */
          typedef Vec3<vfloat<M>> Vec3vfM;
          const Vec3vfM O = Vec3vfM(ray.org);
          const Vec3vfM D = Vec3vfM(ray.dir);
          const Vec3vfM C = Vec3vfM(tri_v0) - O;
          const Vec3vfM R = cross(D,C);
          const vfloat<M> den = dot(Vec3vfM(tri_Ng),D);
          const vfloat<M> absDen = abs(den);
          const vfloat<M> sgnDen = signmsk(den);
        
          /* perform edge tests */
          const vfloat<M> U = dot(R,Vec3vfM(tri_e2)) ^ sgnDen;
          const vfloat<M> V = dot(R,Vec3vfM(tri_e1)) ^ sgnDen;

          /* perform backface culling */
#if defined(RTCORE_BACKFACE_CULLING)
          vbool<M> valid = (den > vfloat<M>(zero)) & (U >= 0.0f) & (V >= 0.0f) & (U+V<=absDen);
#else
          vbool<M> valid = (den != vfloat<M>(zero)) & (U >= 0.0f) & (V >= 0.0f) & (U+V<=absDen);
#endif
          if (likely(none(valid))) return false;
        
          /* perform depth test */
          const vfloat<M> T = dot(Vec3vfM(tri_Ng),C) ^ sgnDen;
          valid &= (T > absDen*vfloat<M>(ray.tnear)) & (T < absDen*vfloat<M>(ray.tfar));
          if (likely(none(valid))) return false;

          /* update hit information */
          QuadHitM<M> hit(U,V,T,absDen,tri_Ng, flags);
          return epilog(valid,hit);
        }
      
        template<typename Epilog>
        __forceinline bool intersect(Ray& ray, 
                                     const Vec3<vfloat<M>>& v0, 
                                     const Vec3<vfloat<M>>& v1, 
                                     const Vec3<vfloat<M>>& v2, 
                                     const vbool<M>& flags,
                                     const Epilog& epilog) const
        {
          const Vec3<vfloat<M>> e1 = v0-v1;
          const Vec3<vfloat<M>> e2 = v2-v0;
          const Vec3<vfloat<M>> Ng = cross(e1,e2);
          return intersect(ray,v0,e1,e2,Ng,flags,epilog);
        }
      };

    template<int M, int Mx, bool filter>
      struct QuadMvIntersector1MoellerTrumbore;

    /*! Intersects 4 quads with 1 ray using SSE */
    template<bool filter>
      struct QuadMvIntersector1MoellerTrumbore<4,4,filter>
    {
      typedef QuadMv<4> Primitive;
      typedef MoellerTrumboreIntersectorQuad1<4> Precalculations;
        
      /*! Intersect a ray with the M quads and updates the hit. */
      static __forceinline void intersect(const Precalculations& pre, Ray& ray, const Primitive& quad, Scene* scene, const unsigned* geomID_to_instID)
      {
        STAT3(normal.trav_prims,1,1,1);
        pre.intersect(ray,quad.v0,quad.v1,quad.v3,vbool4(false),Intersect1Epilog<4,4,filter>(ray,quad.geomIDs,quad.primIDs,scene,geomID_to_instID)); 
        pre.intersect(ray,quad.v2,quad.v3,quad.v1,vbool4(true ),Intersect1Epilog<4,4,filter>(ray,quad.geomIDs,quad.primIDs,scene,geomID_to_instID)); 
      }
        
      /*! Test if the ray is occluded by one of M quads. */
      static __forceinline bool occluded(const Precalculations& pre, Ray& ray, const Primitive& quad, Scene* scene, const unsigned* geomID_to_instID)
      {
        STAT3(shadow.trav_prims,1,1,1);
        if (pre.intersect(ray,quad.v0,quad.v1,quad.v3,vbool4(false),Occluded1Epilog<4,4,filter>(ray,quad.geomIDs,quad.primIDs,scene,geomID_to_instID))) return true;
        if (pre.intersect(ray,quad.v2,quad.v3,quad.v1,vbool4(true ),Occluded1Epilog<4,4,filter>(ray,quad.geomIDs,quad.primIDs,scene,geomID_to_instID))) return true;
        return false;
      }
    };

#if defined(__AVX__)

    /*! Intersects 4 quads with 1 ray using AVX */
    template<bool filter>
      struct QuadMvIntersector1MoellerTrumbore<4,8,filter>
    {
      typedef QuadMv<4> Primitive;
      typedef MoellerTrumboreIntersectorQuad1<8> Precalculations;
        
      /*! Intersect a ray with the M quads and updates the hit. */
      static __forceinline void intersect(const Precalculations& pre, Ray& ray, const Primitive& quad, Scene* scene, const unsigned* geomID_to_instID)
      {
        STAT3(normal.trav_prims,1,1,1);
        const Vec3vf8 vtx0(vfloat8(quad.v0.x,quad.v2.x),vfloat8(quad.v0.y,quad.v2.y),vfloat8(quad.v0.z,quad.v2.z));
        const Vec3vf8 vtx1(vfloat8(quad.v1.x),vfloat8(quad.v1.y),vfloat8(quad.v1.z));
        const Vec3vf8 vtx2(vfloat8(quad.v3.x),vfloat8(quad.v3.y),vfloat8(quad.v3.z));
        const vbool8 flags(0,0,0,0,1,1,1,1);
        pre.intersect(ray,vtx0,vtx1,vtx2,flags,Intersect1Epilog<8,8,filter>(ray,vint8(quad.geomIDs),vint8(quad.primIDs),scene,geomID_to_instID)); 
      }
        
      /*! Test if the ray is occluded by one of M quads. */
      static __forceinline bool occluded(const Precalculations& pre, Ray& ray, const Primitive& quad, Scene* scene, const unsigned* geomID_to_instID)
      {
        STAT3(shadow.trav_prims,1,1,1);
        const Vec3vf8 vtx0(vfloat8(quad.v0.x,quad.v2.x),vfloat8(quad.v0.y,quad.v2.y),vfloat8(quad.v0.z,quad.v2.z));
        const Vec3vf8 vtx1(vfloat8(quad.v1.x),vfloat8(quad.v1.y),vfloat8(quad.v1.z));
        const Vec3vf8 vtx2(vfloat8(quad.v3.x),vfloat8(quad.v3.y),vfloat8(quad.v3.z));
        const vbool8 flags(0,0,0,0,1,1,1,1);
        return pre.intersect(ray,vtx0,vtx1,vtx2,flags,Occluded1Epilog<8,8,filter>(ray,vint8(quad.geomIDs),vint8(quad.primIDs),scene,geomID_to_instID)); 
      }
    };

#endif

#if defined(__AVX512F__)

    /*! Intersects 4 triangle pairs with 1 ray using AVX512KNL */
    template<bool filter>
      struct QuadMvIntersector1MoellerTrumbore<4,16,filter>
      {
        typedef QuadMv<4> Primitive;
        typedef MoellerTrumboreIntersectorQuad1<16> Precalculations;
        
        /*! Intersect a ray with the M triangles and updates the hit. */
        static __forceinline void intersect(const Precalculations& pre, Ray& ray, const Primitive& quad, Scene* scene, const unsigned* geomID_to_instID)
        {
          STAT3(normal.trav_prims,1,1,1);
          Vec3vf16 vtx0(select(0x0f0f,vfloat16(quad.v0.x),vfloat16(quad.v2.x)),
                        select(0x0f0f,vfloat16(quad.v0.y),vfloat16(quad.v2.y)),
                        select(0x0f0f,vfloat16(quad.v0.z),vfloat16(quad.v2.z)));
          Vec3vf16 vtx1(vfloat16(quad.v1.x),vfloat16(quad.v1.y),vfloat16(quad.v1.z));
          Vec3vf16 vtx2(vfloat16(quad.v3.x),vfloat16(quad.v3.y),vfloat16(quad.v3.z));
          vint8   geomIDs(quad.geomIDs); 
          vint8   primIDs(quad.primIDs);        
          const vbool16 flags(0xf0f0);
          pre.intersect(ray,vtx0,vtx1,vtx2,flags,Intersect1Epilog<8,16,filter>(ray,geomIDs,primIDs,scene,geomID_to_instID)); 
        }
        
        /*! Test if the ray is occluded by one of M triangles. */
        static __forceinline bool occluded(const Precalculations& pre, Ray& ray, const Primitive& quad, Scene* scene, const unsigned* geomID_to_instID)
        {
          STAT3(shadow.trav_prims,1,1,1); 
          Vec3vf16 vtx0(select(0x0f0f,vfloat16(quad.v0.x),vfloat16(quad.v2.x)),
                        select(0x0f0f,vfloat16(quad.v0.y),vfloat16(quad.v2.y)),
                        select(0x0f0f,vfloat16(quad.v0.z),vfloat16(quad.v2.z)));
          Vec3vf16 vtx1(vfloat16(quad.v1.x),vfloat16(quad.v1.y),vfloat16(quad.v1.z));
          Vec3vf16 vtx2(vfloat16(quad.v3.x),vfloat16(quad.v3.y),vfloat16(quad.v3.z));
          vint8   geomIDs(quad.geomIDs); 
          vint8   primIDs(quad.primIDs);        
          const vbool16 flags(0xf0f0);
          return pre.intersect(ray,vtx0,vtx1,vtx2,flags,Occluded1Epilog<8,16,filter>(ray,geomIDs,primIDs,scene,geomID_to_instID)); 
        }
      };
#endif


    /* ----------------------------- */
    /* -- ray packet intersectors -- */
    /* ----------------------------- */


    template<int M, int K>
    struct MoellerTrumboreIntersectorQuadMvK
    {
      __forceinline MoellerTrumboreIntersectorQuadMvK(const vbool<K>& valid, const RayK<K>& ray) {}
            
      /*! Intersects K rays with one of M triangles. */
      template<typename Epilog>
        __forceinline vbool<K> intersectK(const vbool<K>& valid0, 
                                          RayK<K>& ray, 
                                          const Vec3<vfloat<K>>& tri_v0, 
                                          const Vec3<vfloat<K>>& tri_e1, 
                                          const Vec3<vfloat<K>>& tri_e2, 
                                          const Vec3<vfloat<K>>& tri_Ng, 
                                          const vbool<K>& flags,
                                          const Epilog& epilog) const
      {
        /* ray SIMD type shortcuts */
        typedef Vec3<vfloat<K>> Vec3vfK;
        
        /* calculate denominator */
        vbool<K> valid = valid0;
        const Vec3vfK C = tri_v0 - ray.org;
        const Vec3vfK R = cross(ray.dir,C);
        const vfloat<K> den = dot(tri_Ng,ray.dir);
        const vfloat<K> absDen = abs(den);
        const vfloat<K> sgnDen = signmsk(den);
        
        /* test against edge p2 p0 */
        const vfloat<K> U = dot(R,tri_e2) ^ sgnDen;
        valid &= U >= 0.0f;
        if (likely(none(valid))) return false;
        
        /* test against edge p0 p1 */
        const vfloat<K> V = dot(R,tri_e1) ^ sgnDen;
        valid &= V >= 0.0f;
        if (likely(none(valid))) return false;
        
        /* test against edge p1 p2 */
        const vfloat<K> W = absDen-U-V;
        valid &= W >= 0.0f;
        if (likely(none(valid))) return false;
        
        /* perform depth test */
        const vfloat<K> T = dot(tri_Ng,C) ^ sgnDen;
        valid &= (T >= absDen*ray.tnear) & (absDen*ray.tfar >= T);
        if (unlikely(none(valid))) return false;
        
        /* perform backface culling */
#if defined(RTCORE_BACKFACE_CULLING)
        valid &= den > vfloat<K>(zero);
        if (unlikely(none(valid))) return false;
#else
        valid &= den != vfloat<K>(zero);
        if (unlikely(none(valid))) return false;
#endif
        
        /* calculate hit information */
        QuadHitK<K> hit(U,V,T,absDen,tri_Ng,flags);
        return epilog(valid,hit);
      }
      
      /*! Intersects K rays with one of M quads. */
      template<typename Epilog>
      __forceinline vbool<K> intersectK(const vbool<K>& valid0, 
                                        RayK<K>& ray, 
                                        const Vec3<vfloat<K>>& tri_v0, 
                                        const Vec3<vfloat<K>>& tri_v1, 
                                        const Vec3<vfloat<K>>& tri_v2, 
                                        const vbool<K>& flags,
                                        const Epilog& epilog) const
      {
        typedef Vec3<vfloat<K>> Vec3vfK;
        const Vec3vfK e1 = tri_v0-tri_v1;
        const Vec3vfK e2 = tri_v2-tri_v0;
        const Vec3vfK Ng = cross(e1,e2);
        return intersectK(valid0,ray,tri_v0,e1,e2,Ng,flags,epilog);
      }
      
      /*! Intersect k'th ray from ray packet of size K with M triangles. */
      template<typename Epilog>
        __forceinline bool intersect(RayK<K>& ray, 
                                     size_t k,
                                     const Vec3<vfloat<M>>& tri_v0, 
                                     const Vec3<vfloat<M>>& tri_e1, 
                                     const Vec3<vfloat<M>>& tri_e2, 
                                     const Vec3<vfloat<M>>& tri_Ng,
                                     const vbool<M>& flags,                                     
                                     const Epilog& epilog) const
      {
        /* calculate denominator */
        typedef Vec3<vfloat<M>> Vec3vfM;
        const Vec3vfM O = broadcast<vfloat<M>>(ray.org,k);
        const Vec3vfM D = broadcast<vfloat<M>>(ray.dir,k);
        const Vec3vfM C = Vec3vfM(tri_v0) - O;
        const Vec3vfM R = cross(D,C);
        const vfloat<M> den = dot(Vec3vfM(tri_Ng),D);
        const vfloat<M> absDen = abs(den);
        const vfloat<M> sgnDen = signmsk(den);
        
        /* perform edge tests */
        const vfloat<M> U = dot(R,Vec3vfM(tri_e2)) ^ sgnDen;
        const vfloat<M> V = dot(R,Vec3vfM(tri_e1)) ^ sgnDen;
        
        /* perform backface culling */
#if defined(RTCORE_BACKFACE_CULLING)
        vbool<M> valid = (den > vfloat<M>(zero)) & (U >= 0.0f) & (V >= 0.0f) & (U+V<=absDen);
#else
        vbool<M> valid = (den != vfloat<M>(zero)) & (U >= 0.0f) & (V >= 0.0f) & (U+V<=absDen);
#endif
        if (likely(none(valid))) return false;
        
        /* perform depth test */
        const vfloat<M> T = dot(Vec3vfM(tri_Ng),C) ^ sgnDen;
        valid &= (T > absDen*vfloat<M>(ray.tnear[k])) & (T < absDen*vfloat<M>(ray.tfar[k]));
        if (likely(none(valid))) return false;
        
        /* calculate hit information */
        QuadHitM<M> hit(U,V,T,absDen,tri_Ng,flags);
        return epilog(valid,hit);
      }
      
      template<typename Epilog>
      __forceinline bool intersect1(RayK<K>& ray, 
                                    size_t k,
                                    const Vec3<vfloat<M>>& v0, 
                                    const Vec3<vfloat<M>>& v1, 
                                    const Vec3<vfloat<M>>& v2, 
                                    const vbool<M>& flags,
                                    const Epilog& epilog) const
      {
        const Vec3<vfloat<M>> e1 = v0-v1;
        const Vec3<vfloat<M>> e2 = v2-v0;
        const Vec3<vfloat<M>> Ng = cross(e1,e2);
        return intersect(ray,k,v0,e1,e2,Ng,flags,epilog);
      }
    };


    /*! Intersects M triangles with K rays. */
    template<int M, int K, bool filter>
      struct QuadMvIntersectorKMoellerTrumbore
      {
        typedef QuadMv<M> Primitive;
        typedef MoellerTrumboreIntersectorQuadMvK<2*M,K> Precalculations;
        
        /*! Intersects K rays with M triangles. */
        static __forceinline void intersect(const vbool<K>& valid_i, Precalculations& pre, RayK<K>& ray, const QuadMv<M>& tri, Scene* scene)
        {
          for (size_t i=0; i<QuadMv<M>::max_size(); i++)
          {
            if (!tri.valid(i)) break;
            STAT3(normal.trav_prims,1,popcnt(valid_i),K);
            const Vec3<vfloat<K>> p0 = broadcast<vfloat<K>>(tri.v0,i);
            const Vec3<vfloat<K>> p1 = broadcast<vfloat<K>>(tri.v1,i);
            const Vec3<vfloat<K>> p2 = broadcast<vfloat<K>>(tri.v2,i);
            const Vec3<vfloat<K>> p3 = broadcast<vfloat<K>>(tri.v3,i);
            pre.intersectK(valid_i,ray,p0,p1,p3,vbool<K>(false),IntersectKEpilog<M,K,filter>(ray,tri.geomIDs,tri.primIDs  ,i,scene));
            pre.intersectK(valid_i,ray,p2,p3,p1,vbool<K>(true ),IntersectKEpilog<M,K,filter>(ray,tri.geomIDs,tri.primIDs+1,i,scene));
          }
        }
        
        /*! Test for K rays if they are occluded by any of the M triangles. */
        static __forceinline vbool<K> occluded(const vbool<K>& valid_i, Precalculations& pre, RayK<K>& ray, const QuadMv<M>& tri, Scene* scene)
        {
          vbool<K> valid0 = valid_i;
          
          for (size_t i=0; i<QuadMv<M>::max_size(); i++)
          {
            if (!tri.valid(i)) break;
            STAT3(shadow.trav_prims,1,popcnt(valid0),K);
            const Vec3<vfloat<K>> p0 = broadcast<vfloat<K>>(tri.v0,i);
            const Vec3<vfloat<K>> p1 = broadcast<vfloat<K>>(tri.v1,i);
            const Vec3<vfloat<K>> p2 = broadcast<vfloat<K>>(tri.v2,i);
            const Vec3<vfloat<K>> p3 = broadcast<vfloat<K>>(tri.v3,i);
            pre.intersectK(valid0,ray,p0,p1,p3,vbool<K>(false),OccludedKEpilog<M,K,filter>(valid0,ray,tri.geomIDs,tri.primIDs,i,scene));
            if (none(valid0)) break;
            pre.intersectK(valid0,ray,p2,p3,p1,vbool<K>(true ),OccludedKEpilog<M,K,filter>(valid0,ray,tri.geomIDs,tri.primIDs,i,scene));
            if (none(valid0)) break;
          }
          return !valid0;
        }
        
        /*! Intersect a ray with M triangles and updates the hit. */
        static __forceinline void intersect(Precalculations& pre, RayK<K>& ray, size_t k, const QuadMv<M>& tri, Scene* scene)
        {
          STAT3(normal.trav_prims,1,1,1);
          Vec3<vfloat<2*M>> vtx0(vfloat<2*M>(tri.v0.x,tri.v2.x),
                                 vfloat<2*M>(tri.v0.y,tri.v2.y),
                                 vfloat<2*M>(tri.v0.z,tri.v2.z));
          Vec3<vfloat<2*M>> vtx1(vfloat<2*M>(tri.v1.x),
                                 vfloat<2*M>(tri.v1.y),
                                 vfloat<2*M>(tri.v1.z));
          Vec3<vfloat<2*M>> vtx2(vfloat<2*M>(tri.v3.x),
                                 vfloat<2*M>(tri.v3.y),
                                 vfloat<2*M>(tri.v3.z));
          vint<2*M> geomIDs(tri.geomIDs); 
          vint<2*M> primIDs(tri.primIDs);
          vbool<2*M> flags(0,1);
          pre.intersect1(ray,k,vtx0,vtx1,vtx2,flags,Intersect1KEpilog<2*M,2*M,K,filter>(ray,k,geomIDs,primIDs,scene)); 
        }
        
        /*! Test if the ray is occluded by one of the M triangles. */
        static __forceinline bool occluded(Precalculations& pre, RayK<K>& ray, size_t k, const QuadMv<M>& tri, Scene* scene)
        {
          STAT3(shadow.trav_prims,1,1,1);
          Vec3<vfloat<2*M>> vtx0(vfloat<2*M>(tri.v0.x,tri.v2.x),
                                 vfloat<2*M>(tri.v0.y,tri.v2.y),
                                 vfloat<2*M>(tri.v0.z,tri.v2.z));
          Vec3<vfloat<2*M>> vtx1(vfloat<2*M>(tri.v1.x),
                                 vfloat<2*M>(tri.v1.y),
                                 vfloat<2*M>(tri.v1.z));
          Vec3<vfloat<2*M>> vtx2(vfloat<2*M>(tri.v3.x),
                                 vfloat<2*M>(tri.v3.y),
                                 vfloat<2*M>(tri.v3.z));
          vint<2*M> geomIDs(tri.geomIDs); 
          vint<2*M> primIDs(tri.primIDs);
          vbool<2*M> flags(0,1);
          return pre.intersect1(ray,k,vtx0,vtx1,vtx2,flags,Occluded1KEpilog<2*M,2*M,K,filter>(ray,k,geomIDs,primIDs,scene)); 
        }
      };

#if defined(__AVX512F__)

    /*! Intersects M triangles with K rays. */
    template<bool filter>
      struct QuadMvIntersectorKMoellerTrumbore<4,16,filter>
      {
        static const int M = 4;
        static const int K = 16;

        typedef QuadMv<M> Primitive;
        typedef MoellerTrumboreIntersectorQuadMvK<16,16> Precalculations;
        
        /*! Intersects K rays with M triangles. */
        static __forceinline void intersect(const vbool<K>& valid_i, Precalculations& pre, RayK<K>& ray, const QuadMv<M>& quad, Scene* scene)
        {
          for (size_t i=0; i<TrianglePairsMv<M>::max_size(); i++)
          {
            if (!quad.valid(i)) break;
            STAT3(normal.trav_prims,1,popcnt(valid_i),K);
            const Vec3<vfloat<K>> p0 = broadcast<vfloat<K>>(quad.v0,i);
            const Vec3<vfloat<K>> p1 = broadcast<vfloat<K>>(quad.v1,i);
            const Vec3<vfloat<K>> p2 = broadcast<vfloat<K>>(quad.v2,i);
            const Vec3<vfloat<K>> p3 = broadcast<vfloat<K>>(quad.v3,i);
            pre.intersectK(valid_i,ray,p0,p1,p3,vbool<K>(false),IntersectKEpilog<M,K,filter>(ray,quad.geomIDs,quad.primIDs  ,i,scene));
            pre.intersectK(valid_i,ray,p2,p3,p1,vbool<K>(true ),IntersectKEpilog<M,K,filter>(ray,quad.geomIDs,quad.primIDs+1,i,scene));
          }
        }
        
        /*! Test for K rays if they are occluded by any of the M triangles. */
        static __forceinline vbool<K> occluded(const vbool<K>& valid_i, Precalculations& pre, RayK<K>& ray, const QuadMv<M>& quad, Scene* scene)
        {
          vbool<K> valid0 = valid_i;
          
          for (size_t i=0; i<TrianglePairsMv<M>::max_size(); i++)
          {
            if (!quad.valid(i)) break;
            STAT3(shadow.trav_prims,1,popcnt(valid0),K);
            const Vec3<vfloat<K>> p0 = broadcast<vfloat<K>>(quad.v0,i);
            const Vec3<vfloat<K>> p1 = broadcast<vfloat<K>>(quad.v1,i);
            const Vec3<vfloat<K>> p2 = broadcast<vfloat<K>>(quad.v2,i);
            const Vec3<vfloat<K>> p3 = broadcast<vfloat<K>>(quad.v3,i);
            pre.intersectK(valid0,ray,p0,p1,p3,vbool<K>(false),OccludedKEpilog<M,K,filter>(valid0,ray,quad.geomIDs,quad.primIDs,i,scene));
            if (none(valid0)) break;
            pre.intersectK(valid0,ray,p2,p3,p1,vbool<K>(true ),OccludedKEpilog<M,K,filter>(valid0,ray,quad.geomIDs,quad.primIDs,i,scene));
            if (none(valid0)) break;
          }
          return !valid0;
        }
        
        /*! Intersect a ray with M triangles and updates the hit. */
        static __forceinline void intersect(Precalculations& pre, RayK<K>& ray, size_t k, const QuadMv<M>& quad, Scene* scene)
        {
          STAT3(normal.trav_prims,1,1,1);
          Vec3vf16 vtx0(select(0x0f0f,vfloat16(quad.v0.x),vfloat16(quad.v2.x)),
                        select(0x0f0f,vfloat16(quad.v0.y),vfloat16(quad.v2.y)),
                        select(0x0f0f,vfloat16(quad.v0.z),vfloat16(quad.v2.z)));
          Vec3vf16 vtx1(quad.v1.x,quad.v1.y,quad.v1.z);
          Vec3vf16 vtx2(quad.v3.x,quad.v3.y,quad.v3.z);
          vint8   geomIDs(quad.geomIDs); 
          vint8   primIDs(quad.primIDs);        
          const vbool16 flags(0xf0f0);
          pre.intersect1(ray,k,vtx0,vtx1,vtx2,flags,Intersect1KEpilog<8,16,16,filter>(ray,k,geomIDs,primIDs,scene)); 
        }
        
        /*! Test if the ray is occluded by one of the M triangles. */
        static __forceinline bool occluded(Precalculations& pre, RayK<K>& ray, size_t k, const QuadMv<M>& quad, Scene* scene)
        {
          STAT3(shadow.trav_prims,1,1,1);
          Vec3vf16 vtx0(select(0x0f0f,vfloat16(quad.v0.x),vfloat16(quad.v2.x)),
                        select(0x0f0f,vfloat16(quad.v0.y),vfloat16(quad.v2.y)),
                        select(0x0f0f,vfloat16(quad.v0.z),vfloat16(quad.v2.z)));
          Vec3vf16 vtx1(quad.v1.x,quad.v1.y,quad.v1.z);
          Vec3vf16 vtx2(quad.v3.x,quad.v3.y,quad.v3.z);
          vint8   geomIDs(quad.geomIDs); 
          vint8   primIDs(quad.primIDs);        
          const vbool16 flags(0xf0f0);
          return pre.intersect1(ray,k,vtx0,vtx1,vtx2,flags,Occluded1KEpilog<8,16,16,filter>(ray,k,geomIDs,primIDs,scene)); 
        }
      };

#endif


  }
}

