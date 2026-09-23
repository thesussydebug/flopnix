static void kext_error_text(char *out,int cap,const char *name,int status,unsigned required,unsigned current)
{
    if(status==43){
        kfmt(out,cap,"%s: E43 needs API %u; kernel has %u. Update kernel.",name,required,current);
        return;
    }
    const char *reason=status==40?"Invalid or unreadable extension":
        status==41?"Unresolved symbol or relocation":
        status==42?"Not enough memory; close an app and retry":
        status==44?"Initialization failed or crashed":
        status==45?"Extension disabled after a fault":"Extension could not load";
    kfmt(out,cap,"%s: E%d %s",name,status,reason);
}
