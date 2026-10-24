#include <cstdlib>
#include <set>
#include <map>
#include <array>
#include "scene/CaveModel.h"
#include "scene/CaveMeshBuilder.h"
#include "scene/TitleAtlas.h"
#include <chrono>
#include <cstdio>
using namespace animated_mojang::scene;
int main(){
  std::string root=std::string(getenv("AM_ROOT"))+"/resources/";
  auto t0=std::chrono::steady_clock::now();
  auto raw=loadCaveModel(root+"data/normal_cave.mesh");
  std::string err; auto atlas=decodeImageRgba(root+"textures/animated_mojang/cave/atlas.png",&err);
  if(!atlas){printf("atlas fail %s\n",err.c_str());return 1;}
  auto mesh=buildCaveMesh(raw,&atlas);
  auto t1=std::chrono::steady_clock::now();
  printf("raw=%zu unique=%zu idx=%zu opaqueTris=%u cutoutTris=%u  (%.1f ms)\n",raw.size(),mesh.vertices.size(),mesh.indices.size(),mesh.opaqueIndexCount/3,mesh.cutoutIndexCount/3,std::chrono::duration<double,std::milli>(t1-t0).count());
  // all indices valid & triangles preserved
  uint32_t mx=0; for(auto i:mesh.indices) mx=std::max(mx,i); printf("max index %u (<65536: %s)\n",mx,mx<65536?"yes":"NO");
  // verify geometry equivalence: rebuild raw positions/uv from indices, compare multiset to raw triangles order-insensitive
  std::multiset<std::array<float,5>> a,b;
  for(size_t i=0;i<raw.size();++i)a.insert({raw[i].position.x,raw[i].position.y,raw[i].position.z,raw[i].u,raw[i].v});
  for(auto i:mesh.indices){auto&v=mesh.vertices[i];b.insert({v.x,v.y,v.z,v.u,v.v});}
  printf("geometry multiset equal: %s\n",a==b?"yes":"NO");
  { // exact per-vertex check: multiset of (pos,uv,quantized color,lava) must be identical
    std::multiset<std::array<float,9>> A,B;
    for(size_t i=0;i<raw.size();++i){auto&r=raw[i]; bool lava=(r.texture&animatedLavaFlag)!=0;
      A.insert({r.position.x,r.position.y,r.position.z,r.u,r.v,(float)detail::toUnorm8(r.r),(float)detail::toUnorm8(r.g),(float)detail::toUnorm8(r.b),lava?255.f:0.f});}
    for(auto i:mesh.indices){auto&v=mesh.vertices[i];B.insert({v.x,v.y,v.z,v.u,v.v,(float)v.r,(float)v.g,(float)v.b,(float)v.lava});}
    printf("full vertex multiset (incl. colors+lava flag) equal: %s\n",A==B?"yes":"NO");
    double maxerr=0; for(auto&r:raw){maxerr=std::max({maxerr,(double)std::abs(r.r-detail::toUnorm8(r.r)/255.0),(double)std::abs(r.g-detail::toUnorm8(r.g)/255.0),(double)std::abs(r.b-detail::toUnorm8(r.b)/255.0)});}
    printf("max color quantization error %.5f (limit 0.5/255=%.5f)\n",maxerr,0.5/255);
  }
  TitleAtlasInfo ti; if(!parseTitleAtlas(root+"textures/animated_mojang/title/baked/title_atlas.json",ti,err)){printf("title fail %s\n",err.c_str());return 1;}
  printf("title pages=%zu frames=%zu resting=%d restPage=%d\n",ti.pages.size(),ti.frames.size(),ti.restingFrame(),ti.restingPage());
  // decode pages
  double ms=0; for(auto&p:ti.pages){auto s=std::chrono::steady_clock::now(); auto im=decodeImageRgba(ti.directory/p.file,&err); ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-s).count(); if(!im||im.width!=p.w||im.height!=p.h){printf("page %s mismatch\n",p.file.c_str());return 1;}}
  printf("decoded %zu title pages in %.0f ms total (host, -O2)\n",ti.pages.size(),ms);
  return 0;}
