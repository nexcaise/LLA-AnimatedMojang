#include <cstdlib>
#include "ll/api/memory/Hook.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <regex>
#include <fstream>
#include <sstream>
// naive reference (original semantics, any alignment)
static std::vector<std::pair<uint8_t,bool>> parse(const std::string&s){std::vector<std::pair<uint8_t,bool>> v;std::istringstream is(s);std::string t;while(is>>t){if(t[0]=='?')v.push_back({0,true});else v.push_back({(uint8_t)strtoul(t.c_str(),0,16),false});}return v;}
static uintptr_t naive(const uint8_t*b,size_t n,const std::string&p){auto pat=parse(p);for(size_t i=0;i+pat.size()<=n;++i){bool m=true;for(size_t j=0;j<pat.size();++j)if(!pat[j].second&&b[i+j]!=pat[j].first){m=false;break;}if(m)return (uintptr_t)(b+i);}return 0;}
int main(){
  std::ifstream f(std::string(getenv("AM_ROOT"))+"/src/client/Client.cpp");std::stringstream ss;ss<<f.rdbuf();std::string src=ss.str();
  std::vector<std::string> pats; std::regex re("static constexpr std::string_view \\w+ =\\s*\"([^\"]+)\";");
  for(std::sregex_iterator it(src.begin(),src.end(),re),e;it!=e;++it)pats.push_back((*it)[1]);
  pats.push_back("?? 7B BA A9 FC 6F"); pats.push_back("DE AD BE EF 01 02 03 04 05 06");
  printf("patterns: %zu\n",pats.size());
  const size_t N=64<<20; std::vector<uint8_t> buf(N+64); std::mt19937_64 rng(7); for(auto&b:buf)b=rng();
  // embed each pattern (concrete bytes for wildcards) at an aligned offset; embed pattern 3 twice (first must win)
  auto embed=[&](const std::string&p,size_t off){auto v=parse(p);for(size_t j=0;j<v.size();++j)buf[off+j]=v[j].second?(uint8_t)(rng()):v[j].first;};
  for(size_t i=0;i+2<pats.size();++i) embed(pats[i], (1000+i*997*4)&~3ull);
  embed(pats[3], 30<<20); // duplicate later in buffer
  auto t0=std::chrono::steady_clock::now();
  auto r=ll::memory::detail::scanRange(buf.data(),N,pats);
  double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
  int bad=0,found=0; t0=std::chrono::steady_clock::now();
  for(size_t i=0;i<pats.size();++i){uintptr_t ref=naive(buf.data(),N,pats[i]); if(r[i])++found; if(r[i]!=ref){ // naive may match unaligned earlier by chance: tolerate only if naive is unaligned & earlier
      if(!(ref && (ref%4) && ref<r[i])) {++bad; printf("MISMATCH %zu new=%lx ref=%lx\n",i,r[i],ref);} } }
  double nms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
  printf("found %d/%zu, mismatches %d | new single pass over 64MB: %.0f ms | naive %zu separate scans: %.0f ms\n",found,pats.size(),bad,ms,pats.size(),nms);
}
