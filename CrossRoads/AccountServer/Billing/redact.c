#include "redact.h"

#include "billing.h"
#include "cmdparse.h"
#include "logging.h"
#include "StringUtil.h"

// Allow redaction to be disabled.
static bool billingDisableRedaction = false;
static bool billingDisableRedactionRequest = false;
AUTO_CMD_INT(billingDisableRedactionRequest, setBillingDisableRedaction) ACMD_HIDE ACMD_CALLBACK(billingDisableRedactionHandler);
static char billingDisableRedactionConfirmed[500] = "";
void billingDisableRedactionHandler(CMDARGS);
AUTO_CMD_STRING(billingDisableRedactionConfirmed, setBillingDisableRedactionConfirmed) ACMD_HIDE ACMD_CALLBACK(billingDisableRedactionHandler);

// Ensure that the operator really wants to disable redaction.
void billingDisableRedactionHandler(CMDARGS)
{
	if (billingDisableRedactionRequest && !strcmp(billingDisableRedactionConfirmed, "never_do_this_on_live"))
		billingDisableRedaction = true;
	else
		billingDisableRedaction = false;
	if (billingDisableRedaction && billingGetServerType() == BillingServerType_Official)
		AssertOrAlert("ACCOUNTSERVER_REDACTION_DISABLED", "Sensitive information redaction has been disabled on the OFFICIAL Account Server!");
	printf("Redaction: %s\n", billingDisableRedaction ? "disabled" : "enabled");
	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Redaction: %s", billingDisableRedaction ? "disabled" : "enabled");
}

// Return the prefix length iff one string starts with another; otherwise return 0.
// Helper function for billingRedact()
__forceinline static bool billingRedact_StartsWith(const char *pString, const char *pStringEnd, const char *pPrefix, ptrdiff_t pPrefixLength)
{
	// Return false if string couldn't possibly match based on length.
	if (!pString || !*pString || !pPrefix || !*pPrefix || !pPrefixLength || pStringEnd - pString < pPrefixLength)
		return false;

	// Do comparison.
	if (!_memicmp(pString, pPrefix, pPrefixLength))
		return pPrefixLength;
	return 0;
}

// Return true if this position might be the beginning of an XML element in the string, based on heuristics.
// Helper function for billingRedact()
__forceinline static bool billingRedact_IsXmlElement(const char *pPos, const char *pString)
{
	// Not XML if at beginning of the string.
	if (pPos == pString)
		return false;

	// It is an XML element if the previous character was a '<'.
	if (pPos - pString >= 1 && pPos[-1] == '<')
		return true;

	// It is an XML element if the previous two characters were "</".
	if (pPos - pString >= 2 && pPos[-2] == '<' && pPos[-1] == '/')
		return true;

	// Otherwise, it is not an XML element.
	return false;
}

// Redact between open and close tags.
__forceinline static bool billingRedact_Remove(char **pos, const char *pBufferEnd, const char *pOpen, const char *pClose)
{
	// Remove the body of XML password elements.
	if (billingRedact_StartsWith(*pos, pBufferEnd, pOpen, strlen(pOpen)))
	{
		char *begin = *pos + strlen(pOpen);
		char *end = strstri(begin, pClose);
		if (!end)
			end = begin + strlen(begin);
		memset(begin + 1, 'X', end - begin - 1);
		*pos = end + strlen(pClose) - 1;
		return true;
	}

	return false;
}

// Using heuristics, remove sensitive billing- and authentication-related information from a string.
const char *billingRedact(const char *pString)
{
	const int uMaxSize = 1024*1024*1024;	// Limit log lines to 1 GB.
	static char *pBuffer = NULL;
	int len;
	char *pos;								// Scan position.
	char *pBufferEnd;
	PERFINFO_AUTO_START_FUNC();

	// If redaction has been disabled, do nothing.
	if (billingDisableRedaction)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return pString;
	}

	// If the string is empty or null, return it.
	if (!pString || !*pString)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return pString;
	}

	// Check string length.
	PERFINFO_AUTO_START("copy", 1);
	estrClear(&pBuffer);
	len = (int)strlen(pString);
	estrConcat(&pBuffer, pString, MIN(len, uMaxSize));
	if (len > uMaxSize)
	{
		estrConcatf(&pBuffer, " [Truncated from %d]", len);
		len = estrLength(&pBuffer);
	}
	pBufferEnd = pBuffer + len;
	PERFINFO_AUTO_STOP();

	// Scan through string character-by-character for substrings to redact.
	PERFINFO_AUTO_START("scan", 1);
	for (pos = pBuffer; *pos; pos = UTF8GetNextCodepoint(pos))
	{
		static const char pPasswordRaw[] = "password";
		static const char pAnswerRaw[] = "answer";
		size_t matchLength;

		// Remove the following:
		// -Passwords
		// -Credit and debit card numbers
		// -CVN codes
		if (billingRedact_Remove(&pos, pBufferEnd, "<password", "</password")
			|| billingRedact_Remove(&pos, pBufferEnd, "CreditCard\"><account", "</account")
			|| billingRedact_Remove(&pos, pBufferEnd, "DirectDebit\"><account", "</account")
			|| billingRedact_Remove(&pos, pBufferEnd, "<item xsi:type=\"vin:NameValuePair\">", "/nameValues>"))
			continue;

		// If any other possibly sensitive strings are present, redact the remainder of the string.
		if ((matchLength = billingRedact_StartsWith(pos, pBufferEnd, pPasswordRaw, sizeof(pPasswordRaw) - 1))
				|| (matchLength = billingRedact_StartsWith(pos, pBufferEnd, pAnswerRaw, sizeof(pAnswerRaw) - 1))
			&& !billingRedact_IsXmlElement(pos, pBuffer))
		{
			memset(pos + matchLength, '?', pBufferEnd - pos - matchLength);
			break;
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP_FUNC();
	return pBuffer;
}

AUTO_COMMAND;
void billingRedactTest(void)
{
	const char * pLongCat = \
"                                                                                                                                                      " \
"                                                                                                                                                      " \
"                                                        .                                                                                             " \
"                                                       ,,....                                                                                         " \
"                                                       .   .,,                                                                                        " \
"                                                      .  ....,,                                                                                       " \
"                                                     .:..,:::::.                                                                                      " \
"                                                     :r:..,::ii:.                             .                                                       " \
"                                                     :vi:::iiiii:                           .:.                                                       " \
"                                                     ,L7ii;r777Lr,                       ..::.                                                        " \
"                                                     .7YY7vLLYvri,.         .. .. ...   .:::                                                          " \
"                                                     :LuSSYv:     ...          ..     .iii:                                                           " \
"                                                     rJYv:     .    ... .            ,iir:                                                            " \
"                                                    .::.    i7YU17.                   .:i                                                             " \
"                                                        .v55SPESFkU:  .,..            .:.                                                             " \
"                                             .         ,PPPXEBBPNM8u ,BBMkYLvri::.     :.                                                             " \
"                                                       uM8XkZGSS8MES:rBBMG2juuL7LLLYr   :                                                             " \
"                                                  .   iUXXSkPqPNZqXuUBBB0MXJjuUYYqBBBO. ..                                         .:;:.:r:.          " \
"                                                 i  rLJuj5P0EMM85Yi:LNEq0G0Y7riiYBBBBBr  ,                    ....,,,.,,.,...,,,:rvJuSUYuPS:          " \
"                                              .:ri:iuSXF5FuYjuJ7iiii::iiiii:,    5BBBMY. .,:,.            .:ir7LYjUjuUuUJuJ7rjuU25F5X0XPOBOv          " \
"                                           ...:rvLJ2215UU7::i;r77r777;ii,.,:::    r127ii:             .,:i7LjuuU15S5FSFF55S005U1FSXGBBMBBX.           " \
"                                         .rrirJ221152UUJri7LLLYuUjjYLLvv7r;rJkSJr. .,.:Lr       .,::iir77YjU2Uu2FSFF5kqXX0MMZN0Z8MMBOEPk:             " \
"                                        .r7rLUUUUUUUUUU7iY122UUUUjJYYLLLLY7rjMMZEr:ir;7v:   .::iir7vLrYUjJjUU1U15FXq0qFYrrrr;rr7rr:                   " \
"                                      .,i77YjUUUUUUuuUuj2F552jYYjLLLLvYuuUUuJUEE1;irLL7i..,::rr77jLLLvU2jJjU222SSSkF7.                                " \
"                                 .   .i77LYjuuuUUUujUu1FS2jJLLjuLYUUuYuU15kXPGO5uUJ7vYYr.,i;r7LLv2uLJYUUUuuU255FSji                                   " \
"                                ....:7YLLYJjUuuUUuU25kS112ujU1ULLYLLjU55SkPNZZ0k2UuYJYvi:7uuYvLLLUUjUUUUu22U25Sj:                                     " \
"                                i:::iU1uJYYuUU2U215SkkkkF1U2UuLjUYuujjJujuUUUUuuuuuujY777Lj22uuYYjuU22U2222Uj7:                                       " \
"                                ,YJJJuU51UU2U25SkS515FFF5Sk551u2jUF22UU1UjjuJjjUUUjJYYYJUuLLYUUUUuUU25FF2Jr,                                          " \
"                                   :jr7U22UUUuU5X1UU225112155FFF1111151F1UU22UUUUuJU221UUU1UJj1FFSkF5ULr,                                             " \
"                                    .;7JYjujjuJu5uuUU22UU2512U251UUuuUUujjjuYLL77JMGMEZ0qXSSXS151j7i:.                                                " \
"                                    .r7LYJjjJjjjujuU51U2251152U155uUuujuJYYYLL7rir7:r7LJYLJvri,.                                                      " \
"                                    .rrrYYJjJjUUUu215FF51UuU222U1SSSF51UuJYYYv7r;;.                                                                   " \
"                                     ,irLYJJYjUUUU2U212uuUUUUUUuuU215S5UuJYYYv7rrr,                                                                   " \
"                                     ,ii7YYYYYjjuuUU22UU25F1UUuUUUuuuuUujYYLLLv7ri.                                                                   " \
"                                    .::i7LYYYYJjuUjUU25SF1151UuU12UUUUjJYYJYYLv7r,                                                                    " \
"                                    .:ir7LYLYYJjuUjUUUUU2U1UU21512UjjujYYYYJYLvLi                                                                     " \
"                                    ,:;777LLYYYYjuuUUUU22155XNSS51UUUuujjjJYYLYv.                                                                     " \
"                                    ,ir777vLYYYJJJuuuuuUuu252XP5UU255UUUUjJJYYJi                                                                      " \
"                                    ,;777r7LYYYYYJjuU251uJuUJJFk1U122uu12uYYYjL.                                                                      " \
"                                    :ir7777vYYYYYjjuuU5F2jjUUuu2151UUuu2UuYLLJr                                                                       " \
"                                   .i;r7v7LLLYYYYjUUuuUFF2UU51uUUu12UuUUUjYLLur                                                                       " \
"                                   .iir7vLYvvYLYJjuUuJu22FS2uU252UUUU2UUUJLLLur                                                                       " \
"                                   .iir777v777LYYYYjUjJJ22U5SFU2522UU2UUuYLLvJ7                                                                       " \
"                                   ,r;777v7v7vLYLYYJuUUuUUUUuuU212U222UUjYLLLYL.                                                                      " \
"                                   .;ir7777LLLvLYYYYjjUUUU21UU5121112UUjJYYYLLJi                                                                      " \
"                                    rr7rrrvv7v7LYJYjJjjuJjU1552UU2552UuuJYjJYLYY:                                                                     " \
"                                    rr7rrr7vLLLLLYYJJuU2Ujju2552UU25F5UuJJJYL77Jr                                                                     " \
"                                    r7L7rr77LLYLLYYJUU2U251Uu1F1U2UuuUUuJjjYYLvYv.                                                                    " \
"                                    rrLYvrr7LLYJjjJjUuUuuU51UU1F2UuuuuuuJYJJYYLLL:                                                                    " \
"                                    ir7JjY7LLYJjuuuJjjuujU1511U1FF2UUUUujYYYLL7r7i                                                                    " \
"                                    :;;7JJjYLLLYjUUUUUU1UjU222U21FSSSF2UjJYYL77rri                                                                    " \
"                                    .iirLJjJYYYJjjUUU122U212UUuuuuUU212UjYYLLv7r;r.                                                                   " \
"                                    ,:i;7YYYJJujJJJJJu221F51UUu2UUuUuuujYYYYLL7rii,                                                                   " \
"                                    ::ir7YYLYJjjujJJuU251122UU2222UUujJJJYJYYL7riii                                                                   " \
"                                    ::r7vvYYYYYuUuUU22U2U22255F52UUJjujJJYYYYL7riii.                                                                  " \
"                                    :i7777LLYYYjjuUuUUU2U5SF0qkF12U1UUUujYYYYL7ri:ii                                                                  " \
"                                   .:i7777vLYYJJYJuuUuUujj2UUFk1UU151UjjjuJYYv7rri:i:                                                                 " \
"                                   ,ir77777LYYYJYJuuU152jjuuJjFS112UUujJjJJYYL7rri::i.                                                                " \
"                                   :i;r777vLLYYYYjUuU21F2uu11uj2211UujJYYLYYYL7rri:::,                                                                " \
"                                   :i;7vLLLvLYYYYjUUjj55152U222Uju22UuujjYYYLv7rri::i:                                                                " \
"                                   :ii77vL77LYYYjjjjJYj25552UU11UUUU1UUUJYYYL77rri::ii.                                                               " \
"                                  .:ir777vvLYYYJjJjUUJYjUUU5S2U1522UUUuujuJYL7rrr;i::i.                                                               " \
"                                  .iir7r777LLYYYjjU11U2UuuUUJu2U212UUujJYYYJYv7rr;iii;.                                                               " \
"                                   iir77r7vv7LJJjUUUUU1FUu1115SSSFUuUUUUUuYYLv777riiir.                                                               " \
"                                   :i777r7v7LYYYjuujju25FUJ2212USS2U2UUUUujYL77rrr;i;i                                                                " \
"                                   ,irr7r7vLYYYJjjujjuUU5F1SFU2U1F2UUUujujYYLv7rrrrii:                                                                " \
"                                   :iir7777vLLYJjujjjuUUUPSU122UU1UUUUUuJJYYYL7777rii:                                                                " \
"                                   i:ir7r77LYYJjjjJjUU1UUUUUUFFFFUUUUUUUUujYYv777rri::                                                                " \
"                                   i:i;777vYYYJjjuujjjUuuju1FkFkX1U2UUujjJYYLv7rrrii::                                                                " \
"                                   iiir777vvLLYjuuU2UUUU21122U2UU22UuJYJJJYYLv7rrrii::.                                                               " \
"                                   iiir77LLvLLJjJjUUu2222UJYjU2UU12211UuuJYYLvv77r;i:i.                                                               " \
"                                   iiir7LLvvvYJYYJJJjU22uUSXX21XF122225122uLLLL77riiii.                                                               " \
"                                   :iii7LL77LYYYJjJJuU21SSFXXXPS151UJYJujujYLLL77riii:.                                                               " \
"                                   :;iirvv7vLLYJjjjjuU1115UUU11ujUF5UuuJJYYJYYv77rrii:.                                                               " \
"                                   irir777v77YYJJjuUUU1UU12jYJ1kF2122kS5UJYLLYL77r;iii.                                                               " \
"                                   :r;rr7777LYYYYjjuuU2uJUUU1kqX522UUUUUUJYJLvvvv7riii,                                                               " \
"                                   .r;irrr77LYYJJjjujj22uuu21kq0PUu2uJYYjYYJYLLLv7r;ii,                                                               " \
"                                    ;rii;7LLLLYJjuUuuUUUU5XF2UU1kF2UUjjJLLYYYLLvv7rri::.                                                              " \
"                                    :rii7vvLLLYYYJuuU51uu2Xk1uUUU11UuUUUjYLLYYLvv77r;ii:                                                              " \
"                                     :rrrr7vLYJYYjjUUU121uJjUuuUUUUuUUUjYL7YuuYLLL7r;ii:                                                              " \
"                                     .rir77vYYLLYjjjjuUj22jU222U22U22UJYYYLLYuuYL7r;ii:,                                                              " \
"                                     ,rir7vLLYLYYJJJJU25PFFS1U2155551UU5FFUYLYYYL7rr;ii.                                                              " \
"                                     ,rr7LLLLLYYYJuUuUUXX25FUjUFP1211251UuSXF2uYLv77;ii,                                                              " \
"                                     .7777vvLLLLYYJuUuuF2U2UUUU2kF1122UjjYJjUUuYYL7riii,                                                              " \
"                                      77r7vvLLLYYJjjuU1FU1FSFFSF15OEF5UjYYL7YJYLLv7rrrr,                                                              " \
"                                      ,7r7vLLYYJjjjjuU5X1225SSSX51kP122UUjUUUUjYL77r;ii,                                                              " \
"                                       :7vvLLYYYJjuujU12FS1uujjuUUjFk2UjU2jYjJJYY777rii:                                                              " \
"                                       i777LYYYYYYJJJu2UU5FUuJYjUF5FqPFUJuuuU1UUjYvr;ii.                                                              " \
"                                       :777LYYYYYLYjJu12UUUUuuU1FZE11qX1jYU5SX2uJYL7rii.                                                              " \
"                                        r7vLvLYYYYYju2UuU12225FF5FqqSkk51uYj21UJYLL7rii.                                                              " \
"                                        :v7vLLYYYYJju1qF2XNNNk51UU5XF552UUjvYjYLL7L7rr7.                                                              " \
"                                        .7vLLLYYYJuU2U52U25NEPS2UjuS0XF12UUuYjJYL77rrri                                                               " \
"                                        .7vLLLLYYYuuUuUUuUU5SFFXq5UUFXS52UkXUjuuYL7rrr.                                                               " \
"                                        i77LL7LYYYjujju2UuUUU2225SF125kkS5ujUUuJYYL77r.                                                               " \
"                                        r7r777LYYjjju2Uu22UUjU11UU5kF511221FFUujLLv7r7.                                                               " \
"                                        77r7777YYjjJu2251511UUU5Fkk5F12155SqFjJYYLv7r;                                                                " \
"                                       .L7rr;;7LYYYjuuU51SPNqk522511515SFX1UUjuuJYL7ri                                                                " \
"                                       .v7iiir7vYJjuuuU2225kFkXF12225SFXNkJJuu1uYvv7ri:                                                               " \
"                                        7rii;77LYJYJjuU1ujuUU2FXXkPPkkXXXNqFYYU2Y77r;:i.                                                              " \
"                                        7riii77vYYYjUUU22UujuU5kSkXkSkXPNXFF1uU2UYvri:i:                                                              " \
"                                        7;iirrrvYYJuUUu1511UUUUU1FFSXqXXq2ju1jLjYv7ri:::                                                              " \
"                                        r;irr;rvYYJjjuuU1UUU1121SkkkXXXFkFjuu2UjY77riiii                                                              " \
"                                        r;irrr7LYJYJuUuU22u2512FkXqXN0PFU1ujYUUujJ7riii;.                                                             " \
"                                       .rrr7rr7YJYYjuUUU11uuU21FSPqPXXPkSkFUuujYLLvriiir,                                                             " \
"                                       :77v7r7LYYYjuJuUU11UUUU5kkP0EqXqPkSk5UJ2UY7v7;i;r,                                                             " \
"                                       ir7777vLYLYjujjUUU12UU255kPqqXkSSPkF21ujUuLv7riir:                                                             " \
"                                      .ir7vvvLYLYYjuJjuUuU52UU25FPNXXPPFSS5FFUuuYJL7rr;ri                                                             " \
"                                      ,,r777vYYYJjJYJuujuU22UU25SXPkkX0kU21U2UjUjjjvri;ri                                                             " \
"                                    :,.,rv7vLYJJjJYYjUuJj212UUUUUFkSqPNPU1FUUjJJjJY7iiiii                                                             " \
"                                  .,. ,:ivv7LJJJYYJJJuuuuU22212122kXNEkSFk522JYLv7rrii:ii                                                             " \
"                                ... .:::;77vLYJYYLYYJjJJUU22U11FSX00qP51F1U2SUL7rrrrri:i:                                                             " \
"                                  .:i::ir7vvLYjYLLLYJYjjjUU2UU215FkENS5F5uUFFJLv77rrriii,                                                             " \
"                      .         .:i:::ir77vvLJjL77LYYYJjJuuU1U2221FPNXPk25F1UUYLvv7;iii;,                                                             " \
"                             .,:i;riir7vv777YJYv7LYLYjjjJujj2U155FFkXNPSF525U2J77r7r:iii:                                           .                 " \
"                        ..,,::ir7L77vvLJJY7vYYYLLLLYJjjJjJuUuuU55SqqPXkS125SFUYvr7rri::::                                                             " \
"                      ..,::iirLJYYJJJJuU2u77vLYYLLYJJJYJjjJUUUuU15kPkkS55111u2JYL7rii::::                                                             " \
"    .,...      .,::::::iiirLYjUujUjJU221FSJ777v7r7LYYJjJjuuU21215SkkFSFF151uUUYJULrrri::::                                                            " \
"    :::ii:::::.,iir7LLvLYjUU22212U25FkXS1i:L7rri;77LYYYJjUUu2U25FSXkSFSSS5UuuJJujL7r7ri::;:                                                           " \
"    :r777vLYYLvYUuUUUjU211515FSSSXPqSUr,   Y7r;r7LLYYjjJuUUj21U2FSkkSXXkS5UUUujUUYLv77;i:ii                                                           " \
"    .7uU115kkPq000E000E0NNPSF155u7i,       L7rr7LYjjYuujuuUUU1115SSSSXXkS1121u1UjLLYLvri::i                                                           " \
"      :LU1SX1UUj7ri::::,...                777vvYJJjjjjuujUUU215FF1FSkXX5215UU2UjJYJLLv7i:i.                                                          " \
"                                           rLLLLJujJjuUjJuUUU215Fk5FFSkk12125UuUUJYYL777r:i,                                                          " \
"                                           iJLLYYYjJjUjjuUuUUU2FS,UX5Fk52551152UuJYYL7rrrii:                                                          " \
"                                           :jYLLYYYYJJUUUUuUUuUPu ,Z55F55FS51UU22UjYYLrr;i;;                                                          " \
"                                           .jYLYYYJuuUUUU22uuU1Zr  L0FF5FS55Uuu12uuuJLL7r;r;                                                          " \
"                                            LuJjYuUUUUU2212UUu5Zr   LPS5FF1UU2U2UU1uYYLv7r7i                                                          " \
"                                            i5uYYjjjUUUUUU22UUFNi    vPF552U222UU22uYY77r7L,                                                          " \
"                                             U2JJuuUUUU1F1UU12Sq:     Yk112112UJU51jYYYLvL7                                                           " \
"                                             ,1UuUUUU2U1F1uujUSu      ,k22151uYJU2UUJYLvLJi                                                           " \
"                                              :FUUUUUU21222UuUq7       1k551jJu221ujYLL77Y.                                                           " \
"                                               vS2UU212U2U12UUN7       70F1jJU5112ujjYjYLY.                                                           " \
"                                               .F1UU22uU21UUUUX7       .Nkuj252UujuuYYv7Y7                                                            " \
"                                                Lk2UUU22FF22UUkY        ik112UjUU2F1uYL7j:                                                            " \
"                                                 jkU215FSS1112kJ         kkUUuu12211UYYYY                                                             " \
"                                                 .1512112221U2kL         1P2uuUUU2UjYLLur                                                             " \
"                                                  vFUUUU22uuU2Sv         YNUuUUjuU52UL7Ui                                                             " \
"                                                  rk12UU11UuU5ki         iN2U2UJJU1uJYLj:                                                             " \
"                                                  ,252UUU11Uu1S.         ,0k22uYYJJYv77L.                                                             " \
"                                                   7S2U2255UuU2.          5quYYLYYv77r;r                                                              " \
"                                                   .uujujJjJYju.          i01JYL7;;iii:i,                                                             " \
"                                                   :YLLL777vLJU.           5PuJLrii:::ii:                                                             " \
"                                                   7YLvvvLLvLJU.           iP1Y7;:::::::i.                                                            " \
"                                                   LYvv777rrrLU,            PPj7iiii:,.:;,                                                            " \
"                                                   Lvrrr;;i;r7Y             vZ1vri::::,.::                                                            " \
"                                                   L7ririi;rrv7              5Njri:::::..:.                                                           " \
"                                                   v;i::iiiiiv7              ,E17rii::::,::                                                           " \
"                                                  .7;iiiiiiiiv7               vNj7ri;ii::::                                                           " \
"                                                  ,L77rr;;;;r77                XF7;:ii::,,,                                                           " \
"                                                  i7ii::iriiirr                :k7:,,,::,.,,                                                          " \
"                                                 .7::::::ii:.:i                 LY:::..:,....                                                         " \
"                                                .::,..,,,,:,.,i.                .Yi::,..,,,..,,,.                                                     " \
"                                               rYi..::.::.i1iirr                 :Ur;iivr:i:,..:7.                                                    " \
"                                             iNOU,..5L.,:,,OBEZr                  5BBP0B7::::,..ir                                                    " \
"                                             :G0::rYS8kYrr:UBBr                    NBBBEvr77LUkFr                                                     " \
"                                              L8ukG80MBBBBZ                          vZBBBMMMBBB7                                                     " \
"                                               jNMBBBBBBBu                             :FBBBBBBU                                                      " \
"                                                  i2X1L:                                  .:::                                                        " \
"                                                                                                                                                      " \
"                                                                                                                                                      " \
"                                                                                                                                                      " \
"                                                                                                                                                      " \
"                                                                                                                                                      " \
"                                                                                                                                                      " \
"                                                                                                                                                      " \
"                                                                                                                                                      " \
"                                                                                                                                                      " \
"                                                                                                                                                      " \
"                                                                                                                                                      " \
"                                                                                                                                                      " \
"                                                                                                                                                      " \
"                                                                                                                                                      " \
"                                                                                                                                                      ";
	S64 start = timerCpuTicks64();
	int i;

	for (i = 0; i < 1000; i++)
		billingRedact(pLongCat);

	printf("Ticks: %" FORM_LL "i\n", timerCpuTicks64() - start);
}