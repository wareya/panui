#define __CRT__NO_INLINE

char * __cdecl
stpcpy(char * __restrict__ _Dest,const char * __restrict__ _Source)
{
  for (; *_Source; _Source++, _Dest++)
    *_Dest = *_Source;
  *_Dest = '\0';
  return _Dest;
}
