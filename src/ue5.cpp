#include "abiotic/ue5.hpp"
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>
namespace abiotic {
namespace {
bool finitePos(const Vec3& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::abs(p.x)<5e7&&std::abs(p.y)<5e7&&std::abs(p.z)<5e7;}
bool begins(const std::string& s,const char* pref){return s.rfind(pref,0)==0;}
Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
}
void UE5::clearCaches(){names_.clear();classes_.clear();}
std::string UE5::name(std::uint32_t id){
    if(auto it=names_.find(id);it!=names_.end())return it->second;
    if(id>=0x4000000)return {};
    const Addr blocks=p_.base()+offsets::NameBlocks;
    auto block=p_.pointer(blocks+(id>>16)*8ULL);
    if(!block)return {};
    Addr addr=block+(id&0xffffULL)*2ULL;
    auto header=p_.get<std::uint16_t>(addr);
    if(!header)return {};
    const unsigned len=*header>>6;
    if(len==0||len>256)return {};
    bool wide=(*header&1)!=0;
    std::string result;
    if(wide){
        std::vector<wchar_t> buf(len);
        if(!p_.read(addr+2,buf.data(),len*2))return {};
        for(auto c:buf){if(c<32||c>126)return {};result.push_back((char)c);}
    }else{
        result.resize(len);
        if(!p_.read(addr+2,result.data(),len))return {};
        for(unsigned char c:result)if(c<32)return {};
    }
    if(names_.size()>300000)names_.clear();
    names_.emplace(id,result);return result;
}
Addr UE5::classOf(Addr obj)const {return validPtr(obj)?p_.pointer(obj+offsets::UObjectClass):0;}
bool UE5::saneObject(Addr obj)const {
    if(!validPtr(obj))return false;
    Addr cls=classOf(obj);
    return cls && cls!=obj && classOf(cls);
}
std::string UE5::className(Addr cls){
    if(auto it=classes_.find(cls);it!=classes_.end())return it->second;
    if(!saneObject(cls))return {};
    auto id=p_.get<std::uint32_t>(cls+offsets::UObjectName);
    if(!id)return {};
    auto n=name(*id);
    if(classes_.size()>10000)classes_.clear();
    if(!n.empty())classes_.emplace(cls,n);
    return n;
}
bool UE5::array(Addr address,int max,TArray& out,bool allowEmpty)const {
    TArray a{};
    if(!p_.read(address,&a,sizeof(a)))return false;
    if(a.count<0||a.count>a.capacity||a.capacity>max)return false;
    if(a.count==0){if(!allowEmpty)return false;out=a;return true;}
    if(!validPtr(a.data))return false;
    out=a;return true;
}
bool UE5::readChain(Addr& world,Addr& manager)const {
    world=p_.pointer(p_.base()+offsets::GWorld);if(!saneObject(world))return false;
    Addr gi=p_.pointer(world+offsets::WorldGI);
    TArray players{};if(!saneObject(gi)||!array(gi+offsets::GIPlayers,256,players))return false;
    Addr lp=p_.pointer(players.data);Addr pc=p_.pointer(lp+offsets::LPController);
    manager=p_.pointer(pc+offsets::ControllerCamera);
    return saneObject(lp)&&saneObject(pc)&&saneObject(manager);
}
bool UE5::anchorCheck(Addr world){
    // Check fixed build's FNamePool layout using directly observed anchor types.
    Addr level=p_.pointer(world+offsets::WorldPersistent);
    if(!saneObject(level)||!saneObject(world))return false;
    auto idW=p_.get<std::uint32_t>(classOf(world)+offsets::UObjectName);
    auto idL=p_.get<std::uint32_t>(classOf(level)+offsets::UObjectName);
    return idW&&idL&&name(*idW)=="World"&&name(*idL)=="Level";
}
bool UE5::readCamera(Addr mgr,Camera& camera)const {
    std::array<std::uint8_t,0x44> raw{};
    if(!p_.read(mgr+offsets::CameraCache,raw.data(),raw.size()))return false;
    auto f32=[&](size_t o){float f;memcpy(&f,raw.data()+o,sizeof(f));return f;};
    auto v3=[&](size_t o){Vec3 v;memcpy(&v,raw.data()+o,sizeof(v));return v;};
    float stamp=f32(0);camera.pos=v3(0x10);camera.rotation=v3(0x28);camera.fov=f32(0x40);
    return stamp>0&&stamp<1e8&&camera.fov>=10&&camera.fov<=175&&finitePos(camera.pos)&&finitePos(camera.rotation);
}
bool UE5::actorPos(Addr actor,Vec3& pos)const {
    Addr root=p_.pointer(actor+offsets::RootComponent);
    if(!saneObject(root))return false;
    return p_.read(root+offsets::Position,&pos,sizeof(pos))&&finitePos(pos);
}
Life UE5::life(Addr actor,bool robot) const {
    Life out{};
    auto hp=p_.get<float>(actor+offsets::Health);
    auto flag=p_.get<std::uint8_t>(actor+offsets::Death);
    if(!hp||!flag||!std::isfinite(*hp)||*hp< -10000||*hp>100000)return out;
    out.hp=*hp;out.dead=(*flag&1)!=0;out.valid=true;
    if(robot){auto armor=p_.get<float>(actor+offsets::Armor);
        if(armor&&std::isfinite(*armor)&&*armor>=0&&*armor<=100000){out.armor=*armor;out.armorValid=true;}}
    return out;
}
ScreenPoint project(const Camera& cam,const Vec3& pos,int width,int height){
    constexpr double pi=3.14159265358979323846;
    auto angle=[&](double a){return a*pi/180.0;};
    double p=angle(cam.rotation.x),y=angle(cam.rotation.y),r=angle(cam.rotation.z);
    double cp=cos(p),sp=sin(p),cy=cos(y),sy=sin(y),cr=cos(r),sr=sin(r);
    Vec3 f{cp*cy,cp*sy,sp};Vec3 right0{-sy,cy,0},up0{-sp*cy,-sp*sy,cp};
    Vec3 right{cr*right0.x+sr*up0.x,cr*right0.y+sr*up0.y,cr*right0.z+sr*up0.z};
    Vec3 up{-sr*right0.x+cr*up0.x,-sr*right0.y+cr*up0.y,-sr*right0.z+cr*up0.z};
    Vec3 d=sub(pos,cam.pos);
    double depth=dot(d,f), dist=sqrt(dot(d,d));
    if(depth<=1||!std::isfinite(dist))return {};
    double focal=width/(2*tan(angle(cam.fov)/2));
    double screenX=width/2.0+dot(d,right)*focal/depth;
    double screenY=height/2.0-dot(d,up)*focal/depth;
    return {screenX,screenY,dist,std::isfinite(screenX)&&std::isfinite(screenY)&&screenX>=0&&screenY>=0&&screenX<width&&screenY<height};
}
Category categorize(const std::string& n){
    // Include later-game hostile families without guessing individual class names.
    if((begins(n,"NPC_Monster_")||begins(n,"NPC_Robot_")||
        begins(n,"NPC_Zombie_"))&&n.ends_with("_C"))return Category::Hostile;
    // Validated class names observed in user late-game scan reports.
    if(n=="NPC_Exor_Pikeman_C"||n=="NPC_Exor_Monk_C"||
       n=="NPC_Peccary_Mushroom_C")return Category::Hostile;
    if(n=="NarrativeNPC_Human_ParentBP_C"||n=="NarrativeNPC_Human_Hologram_C")return Category::NarrativeNpc;
    if(n=="Abiotic_Item_Dropped_C")return Category::Dropped;
    if(begins(n,"Resource_MicroNode_")&&n.ends_with("_C"))return Category::MicroLoot;
    if(begins(n,"ResourceNode_")&&n.ends_with("_C"))return Category::ResourceNode;
    if(begins(n,"Deployed_Container_")||begins(n,"Container_"))return Category::Container;
    return Category::Unknown;
}
}
