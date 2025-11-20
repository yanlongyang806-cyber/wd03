#pragma once

S32 strtol_fast( const char * str, char ** endptr, int base );
U32 strtoul_fast( const char * str, char ** endptr, int base );
int atoi_fast( const char * str );
S64 atoi64_fast( const char *str );
S64 strtoi64_fast( const char *str, char **endptr, int base);

//some of these have optimized versions in strings_opt.h

#undef strtol
#undef strotoul
#undef atoi
#undef _atoi64
#undef atoi64
#undef _strtoi64

#define strtol strtol_fast
#define strtoul strtoul_fast
#define atoi atoi_fast

#define _atoi64 atoi64_fast
#define atoi64 atoi64_fast
#define _strtoi64 strtoi64_fast
