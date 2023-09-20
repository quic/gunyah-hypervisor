#ifndef __KW_MODERN_ENGINE__
int
__builtin_ffsll(unsigned long long x);
#kw_override compiler_ffs(x) __builtin_ffsll(x)

/* FPs caused by KW not understanding __builtin_expect(), overrides suggested by
 * TomZ '22 */
#kw_override compiler_expected(x)(x)
#kw_override compiler_unexpected(x)(x)

#kw_override HYP_LOG_FATAL(xx_fmt, ...)(abort())
#endif
