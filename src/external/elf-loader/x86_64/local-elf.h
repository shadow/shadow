/* Some older systems do not provide these definitions
   but we can still build into our code support for them:
   it does not hurt
*/
#ifndef R_X86_64_IRELATIVE
  #define R_X86_64_IRELATIVE 37
#endif
#ifndef R_X86_64_PC64
  #define R_X86_64_PC64 24
#endif
#ifndef R_X86_64_GOTOFF64
  #define R_X86_64_GOTOFF64 25
#endif
#ifndef R_X86_64_GOTPC32
  #define R_X86_64_GOTPC32 26
#endif

