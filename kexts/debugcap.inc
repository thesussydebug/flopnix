#define CAP_AGE 200u
// small usb writes are slow, so hold packets until there's a pile or they're stale
static int cap_due(u32 fill,u32 bank,u32 age,int stopping)
{
    u32 batch=bank/2<16384u?bank/2:16384u;
    return stopping||(fill&&(fill>=batch||age>=CAP_AGE));
}
