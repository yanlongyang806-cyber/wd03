// Support for TLS and x509 certificates on Windows using CryptoAPI and Schannel.

// Implementation features
//   -Inter-connection session persistence support
//   -Renegotiation support
//   -Session shutdown support

#define SECURITY_WIN32
#include "wininclude.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <schannel.h>
#include <security.h>

#include "crypt.h"
#include "error.h"
#include "EString.h"
#include "logging.h"
#include "timing.h"
#include "tls.h"
#include "tlsCommands.h"
#include "utf8.h"

#ifndef _WIN32
#error Not supported on this configuration.
#endif

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "secur32.lib")

/************************************************************************/
/* TLS Internal Structures                                              */
/************************************************************************/

// Certificate information
struct TlsCertificateInternal
{
	const CERT_CONTEXT *pContext;				// Context of the loaded certificate
	CredHandle credTlsCredentials;				// Schannel SSPI credentials for this certificate
	bool bCredentialsAcquired;					// True if credTlsCredentials has been acquired with pContext.
	U64 uRefCount;								// Number of TLS sessions using this certificate.
};

// Bitwise flag values for a TLS session
enum TlsSessionFlags
{
	TlsSessionFlag_Ended = 1,								// The TLS session is not currently active
	TlsSessionFlag_Failed = 2,								// Negotiation has failed or the session has been terminated by errors
	TlsSessionFlag_NegotiationStarted = 4,					// TLS negotiation has been started
	TlsSessionFlag_SessionStarted = 8,						// A TLS session has begun
	TlsSessionFlag_NegotiationComplete = 16,				// TLS negotiation has been completed
	TlsSessionFlag_Renegotiate = 32,						// The TLS session needs to be renegotiated
};

// A single TLS session
struct TlsSessionInternal
{
	// Session information
	U32 uFlags;									// Bitwise flags of TlsSessionFlags
	TlsCertificate *pCertificate;				// Certificate for this TLS session
	CtxtHandle ctxtTlsContext;					// TLS context
	char *estrInBuffer;							// Incomplete input
	char *estrCipherBuffer;						// Buffer for data about to be encrypted
	size_t uCipherBufferLimit;					// Maximum size that the message portion of estrCipherBuffer is allowed to grow to.
	unsigned char iInUse;						// True if we're processing data for this session, for reentrancy detection

	// Discovered sizes
	// uMaximumMessageLength is zero if and only if these are uninitialized
	unsigned long uMaximumMessageLength;		// Maximum message length (zero if sizes are uninitialized)
	unsigned long uHeaderLength;				// Length of TLS header
	unsigned long uTrailerLength;				// Length of TLS trailer

	// User data
	void *pUserData;
	TlsSessionDataCallback fpPlainDataReceived;
	TlsSessionDataCallback fpCipherDataSend;
	TlsSessionNotifyCallback fpOpened;
	TlsSessionNotifyCallback fpClosed;
};

// True if Schannel has been successfully initialized by InitializeSchannelIfNecessary().
static bool schannelInitialized = false;

// SSPI functions
static SecurityFunctionTable *sspi = NULL;

// Maximum size of a security token, in bytes.
unsigned long sspiMaxToken = 0;

// Last error information for TryDllLoad().
static int TryDllLoad_LastError = 0;
static const char *TryDllLoad_LastErrorString = NULL;
static const char *TryDllLoad_LastErrorFunction = NULL;
static unsigned TryDllLoad_LastErrorHowFar = 0;

/************************************************************************/
/* TLS Internal Support Functions                                       */
/************************************************************************/

// Note that we are in TLS code.
static void tlsEnter(struct TlsSession *pSession)
{
	devassertmsg(pSession->pInternal->iInUse == 0, "Attempted to re-enter TLS code");
	pSession->pInternal->iInUse = 1;
}

// Note that we are no longer in TLS code.
static void tlsLeave(struct TlsSession *pSession)
{
	devassertmsg(pSession->pInternal->iInUse == 1, "Attempted to leave TLS code without having entered");
	pSession->pInternal->iInUse = 0;
}

// Save the error information for DLL load problems.
static void TrySspiDllLoadErrorRecord(const char *pFunction, const char *pErrorString, unsigned iLevel)
{
	if (TryDllLoad_LastErrorHowFar < iLevel)
	{
		TryDllLoad_LastErrorHowFar = iLevel;
		if (pErrorString)
		{
			TryDllLoad_LastErrorString = pErrorString;
			TryDllLoad_LastError = 0;
		}
		else
		{
			TryDllLoad_LastError = GetLastError();
			TryDllLoad_LastErrorString = NULL;
		}
		TryDllLoad_LastErrorFunction = pFunction;
	}
}

// Call Errorf() with SSPI error information.
static void TrySspiDllLoadError()
{
	devassert(!TryDllLoad_LastError || !TryDllLoad_LastErrorString);
	if (TryDllLoad_LastError)
		WinErrorf(TryDllLoad_LastError, "%s", TryDllLoad_LastErrorFunction);
	else
		Errorf("%s", TryDllLoad_LastErrorString);
}

// Try to load security packages from a particular DLL.
static bool TrySspiDllLoad(const char *pDllName)
{
	HMODULE dll;
	INIT_SECURITY_INTERFACE sspiInitSecurityInterface;
	unsigned long size;
	unsigned long i;
	SecPkgInfo *packages;
	SECURITY_STATUS status;
	bool found = false;
	bool result;
	
	// Open DLL.
	dll = LoadLibraryA(pDllName);
	if (!dll)
	{
		TrySspiDllLoadErrorRecord("LoadLibrary()", NULL, 0);
		return false;
	}

	// Find the SSPI dispatch table initialization function.
	sspiInitSecurityInterface = (INIT_SECURITY_INTERFACE)GetProcAddress(dll, "InitSecurityInterfaceA");
	if (!sspiInitSecurityInterface)
	{
		TrySspiDllLoadErrorRecord("GetProcAddress()", NULL, 1);
		result = FreeLibrary(dll);
		devassert(result);
		return false;
	}

	// Load the security functions.
	sspi = sspiInitSecurityInterface();
	if (!sspi)
	{
		TrySspiDllLoadErrorRecord("InitSecurityInterface()", NULL, 2);
		result = FreeLibrary(dll);
		devassert(result);
		return false;
	}

	// Verify that all of the functions are present.
	// Note that sspi->SetContextAttributes seems to be NULL on Vista 64, even though Schannel documents supporting this function.
	if (!sspi->EnumerateSecurityPackages || !sspi->QueryCredentialsAttributes || !sspi->AcquireCredentialsHandle
		|| !sspi->FreeCredentialHandle || !sspi->InitializeSecurityContext || !sspi->AcceptSecurityContext
		|| !sspi->CompleteAuthToken || !sspi->DeleteSecurityContext || !sspi->ApplyControlToken
		|| !sspi->QueryContextAttributes || !sspi->ImpersonateSecurityContext || !sspi->RevertSecurityContext
		|| !sspi->MakeSignature || !sspi->VerifySignature || !sspi->FreeContextBuffer
		|| !sspi->QuerySecurityPackageInfo || !sspi->ExportSecurityContext || !sspi->ImportSecurityContextW
		|| !sspi->AddCredentials || !sspi->QuerySecurityContextToken || !sspi->EncryptMessage
		|| !sspi->DecryptMessage)
	{
		TrySspiDllLoadErrorRecord(NULL, "There are security functions missing from the dispatch table.", 3);
		result = FreeLibrary(dll);
		devassert(result);
		return false;
	}

	// Get list of security packages.
	status = sspi->EnumerateSecurityPackages(&size, &packages);
	if (status != SEC_E_OK)
	{
		TrySspiDllLoadErrorRecord("EnumerateSecurityPackages()", NULL, 4);
		result = FreeLibrary(dll);
		devassert(result);
		return false;
	}

	// Scan available packages.
	found = false;
	for (i = 0; i != size; ++i)
	{
		SecPkgInfo *pkg = &packages[i];
		if (!wcscmp(pkg->Name, UNISP_NAME))
		{
			found = true;
			sspiMaxToken = pkg->cbMaxToken;
			if (!sspiMaxToken)
			{
				Errorf("The SSPI MaxToken is too small.");
				result = FreeLibrary(dll);
				devassert(result);
				return false;
			}
			if (tlsDebugLevel() >= 2)
				printf("[*] ");
		}
		else if (tlsDebugLevel() >= 2)
			printf("[ ] ");
		if (tlsDebugLevel() >= 2)
		{
			char *pTempName = NULL;
			char *pTempComment = NULL;

			UTF16ToEstring(pkg->Name, 0, &pTempName);
			UTF16ToEstring(pkg->Name, 0, &pTempComment);

			printf("Capabilities = %lu, Version = %u, RPCID = %u, MaxToken = %lu, Name = %s, Comment = %s\n", pkg->fCapabilities, pkg->wVersion,
				pkg->wRPCID, pkg->cbMaxToken, pTempName, pTempComment);

			estrDestroy(&pTempName);
			estrDestroy(&pTempComment);
		}
	}
	if (!found)
	{
		char *pTemp = NULL;
		UTF16ToEstring(UNISP_NAME, 0, &pTemp);
		estrInsertf(&pTemp, 0, "Unable to find security package: ");
		TrySspiDllLoadErrorRecord(NULL,  pTemp, 5);
		estrDestroy(&pTemp);
		result = FreeLibrary(dll);
		devassert(result);
		return false;
	}

	return true;
}

// Initialize Schannel if it has not yet been initialized.
static void InitializeSchannelIfNecessary()
{
	static volatile long mutexInitializing = 0;
	static volatile long mutexInitialized = 0;
	static CRITICAL_SECTION mutex;

	// Acquire mutex.
	while (!mutexInitialized && InterlockedIncrement(&mutexInitializing) != 1)
		InterlockedDecrement(&mutexInitializing);
	if (!mutexInitialized)
	{
		InitializeCriticalSection(&mutex);
		mutexInitialized = 1;
	}
	EnterCriticalSection(&mutex);

	// Only initialize once.
	if (schannelInitialized)
	{
		LeaveCriticalSection(&mutex);
		return;
	}

	// Try to load security provider.
	if (!TrySspiDllLoad("secur32") && !TrySspiDllLoad("security"))
	{
		TrySspiDllLoadError();
		return;
	}

	// Schannel has been successfully initialized.
	schannelInitialized = true;

	// Release mutex.
	LeaveCriticalSection(&mutex);
}

#define EXPIRY_WARN_TIME (45*SECONDS_PER_DAY)
// Check certificate validity.
void CheckCertificateValidity(TlsCertificate *pCert)
{
	static bool expireSoon = false, expireNow = false;
	U32 expires = timeSecondsSince2000FromFileTime(&pCert->pInternal->pContext->pCertInfo->NotAfter);
	U32 now = timeServerSecondsSince2000();
	if (expires <= now)
	{
		if (!expireNow)
		{
			AssertOrAlert("TLS_CERTIFICATE_EXPIRED", "The TLS certificate has expired.");
			expireNow = true;
		}
	}
	else if (expires <= now + EXPIRY_WARN_TIME)
	{
		if (!expireSoon)
		{
			ErrorOrAlert("TLS_CERTIFICATE_EXPIRE_SOON", "The TLS certificate will expire in %d days.", (expires - now)/(SECONDS_PER_DAY));
			expireSoon = true;
		}
	}
}

// Return a pointer to the beginning of the next key in an X.500 name string.
// If none found, return NULL.
static const char *FindNextKey(const char *name)
{
	const char *ptr = name;
	for(; *ptr; ++ptr)
	{
		switch(*ptr)
		{
			// Skip quoted text.
			case '"':
				while (*ptr && *ptr != '"')
					++ptr;
				break;

			// Exit on comma.
			case ',':
				return ptr;
		}
	}
	return NULL;
}

// Return a pointer to the beginning of the common name in an X.500 name string.
// If none found, return NULL.
static const char *FindCN(char *name)
{
	const char *ptr = name;

	// Loop over name keys in the string.
	for(;;)
	{
		// Skip leading whitespace.
		while (*ptr == ' ')
			++ptr;

		// Check if this is a CN key.
		if (!strnicmp(ptr, "CN=", 3))
		{
			ptr += 3;
			while (*ptr == ' ')
				++ptr;
			return ptr;										// Successful return.
		}

		// Get next key.
		ptr = FindNextKey(ptr);
		if (!ptr)
			return NULL;									// Unsuccessful return.
		++ptr;
	}

	// Never reached
	devassert(0);
	return NULL;
}

// Get the name of a particular certificate.
// This is a wrapper for CertNameToStr() that handles allocating the string.
static DWORD tlsCertNameToStrAlloc(DWORD dwCertEncodingType, PCERT_NAME_BLOB pName, DWORD dwStrType, char **estrOut)
{
	DWORD result;

	// Keep trying to call function until the buffer is long enough.
	SetLastError(0);
	while ((result = CertNameToStrA(dwCertEncodingType, pName, dwStrType, *estrOut, estrLength(estrOut)))
		&& GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		estrForceSize(estrOut, estrLength(estrOut) * 2);
		SetLastError(0);
	}

	return result;
}

// Mark a session as complete.
static void SessionComplete(struct TlsSession *pSessionExternal)
{
	struct TlsSessionInternal *pSession = pSessionExternal->pInternal;
	SECURITY_STATUS status;

	// Mark the connection as ended.
	devassert(!(pSession->uFlags & TlsSessionFlag_Ended));
	if (tlsDebugLevel() >= 1)
		puts("Session complete");
	pSession->uFlags |= TlsSessionFlag_Ended;

	// Release security context.
	if (pSession->uFlags & TlsSessionFlag_NegotiationStarted || pSession->uFlags & TlsSessionFlag_Renegotiate)
	{
		status = DeleteSecurityContext(&pSessionExternal->pInternal->ctxtTlsContext);
		devassertmsgf(status == SEC_E_OK, "DeleteSecurityContext(): %d", status);
	}

	// Zero out security context handle.
	// Note: This is set for the same reason as in tlsSessionStartServer().  Please see the comments there.
	pSessionExternal->pInternal->ctxtTlsContext.dwLower = 0; // = 0xdeadbeef;
	pSessionExternal->pInternal->ctxtTlsContext.dwUpper = 0; // = 0xbaadf00d;

	// Notify the server code that the session has finished.
	pSession->fpClosed(pSessionExternal, pSession->pUserData);
}

// Mark a session as failed.
static void SessionFailed(struct TlsSession *pSessionExternal)
{
	struct TlsSessionInternal *pSession = pSessionExternal->pInternal;
	if (tlsDebugLevel() >= 1)
		puts("Session failed");
	pSession->uFlags |= TlsSessionFlag_Failed;
	SessionComplete(pSessionExternal);
}

// Encrypt plaintext data with TLS.
// pData must have uHeaderLength bytes preceding it for the header, and uTrailerLength bytes following it
// for the trailer.  The header bytes will be clobbered, but the trailer bytes will be restored if bSaveTrailer
// is true.
static void EncryptTls(struct TlsSession *pSessionExternal, char *pData, unsigned long uLength, bool bSaveTrailer)
{
	struct TlsSessionInternal *pSession = pSessionExternal->pInternal;
	SecBuffer message[4];
	SecBufferDesc messageDesc;
	SECURITY_STATUS status;
	char *trailerRestore;

	// Return if the session has ended.
	if (pSession->uFlags & TlsSessionFlag_Ended)
		return;
	devassert(pSessionExternal);
	devassert(pData && uLength);
	devassert(pSession->uFlags & TlsSessionFlag_NegotiationComplete);
	devassert(uLength <= pSession->uMaximumMessageLength);

	// Save the region that the trailer will overwrite.
	if (bSaveTrailer)
	{
		trailerRestore = alloca(pSession->uTrailerLength);
		memcpy(trailerRestore, pData + uLength, pSession->uTrailerLength);
	}

	// Set up message buffer.
	message[0].BufferType = SECBUFFER_STREAM_HEADER;
	message[0].pvBuffer = pData - pSession->uHeaderLength;
	message[0].cbBuffer = pSession->uHeaderLength;
	message[1].BufferType = SECBUFFER_DATA;
	message[1].pvBuffer = pData;
	message[1].cbBuffer = uLength;
	message[2].BufferType = SECBUFFER_STREAM_TRAILER;
	message[2].pvBuffer = pData + uLength;
	message[2].cbBuffer = pSession->uTrailerLength;
	message[3].BufferType = SECBUFFER_EMPTY;
	message[3].pvBuffer = NULL;
	message[3].cbBuffer = 0;
	messageDesc.ulVersion = SECBUFFER_VERSION;
	messageDesc.pBuffers = message;
	messageDesc.cBuffers = sizeof(message)/sizeof(*message);

	// Encrypt the message.
	status = EncryptMessage(&pSession->ctxtTlsContext, 0, &messageDesc, 0);
	switch (status)
	{
		case SEC_E_OK:
			break;

		case SEC_E_INSUFFICIENT_MEMORY:
			SessionFailed(pSessionExternal);
			Errorf("Out of memory while decrypting TLS");
			return;

		// Internal runtime error
		case SEC_E_INTERNAL_ERROR:
			SessionFailed(pSessionExternal);
			Errorf("Internal error while decrypting TLS");
			return;

		case SEC_E_CONTEXT_EXPIRED:
		case SEC_E_CRYPTO_SYSTEM_INVALID:
		case SEC_E_INVALID_HANDLE:
		case SEC_E_INVALID_TOKEN:
		case SEC_E_QOP_NOT_SUPPORTED:
			devassertmsgf(0, "EncryptMessage(): %lx", (unsigned long)status);
			SessionFailed(pSessionExternal);
			return;

		default:
			WinErrorf(status, "Unknown SSPI error while encrypting data");
			SessionFailed(pSessionExternal);
			return;
	}

	// Send data to peer.
	devassert(!message[3].pvBuffer && !message[3].cbBuffer);
	if (message[0].pvBuffer == pData && (char *)message[0].pvBuffer + message[0].cbBuffer == message[1].pvBuffer
		&& (char *)message[1].pvBuffer + message[1].cbBuffer == message[2].pvBuffer)
		pSession->fpCipherDataSend(pSessionExternal, pSession->pUserData, message[0].pvBuffer,
			(char *)message[2].pvBuffer + message[2].cbBuffer - (char *)message[0].pvBuffer);
	else
	{
		devassert(message[0].cbBuffer && message[1].cbBuffer && message[2].cbBuffer);
		pSession->fpCipherDataSend(pSessionExternal, pSession->pUserData, message[0].pvBuffer, message[0].cbBuffer);
		pSession->fpCipherDataSend(pSessionExternal, pSession->pUserData, message[1].pvBuffer, message[1].cbBuffer);
		pSession->fpCipherDataSend(pSessionExternal, pSession->pUserData, message[2].pvBuffer, message[2].cbBuffer);
	}

	// Restore that data overwritten by the trailer.
	if (bSaveTrailer)
		memcpy(pData + uLength, trailerRestore, pSession->uTrailerLength);
}

// Send data from the cipher buffer to EncryptTls() in appropriately-sized chunks.
static void EncryptFromBuffer(struct TlsSession *pSessionExternal)
{
	struct TlsSessionInternal *pSession = pSessionExternal->pInternal;
	size_t i;

	// Break the cipher buffer into message-sized blocks, with room for the negotiation header.
	for (i = pSession->uHeaderLength; i < estrLength(&pSession->estrCipherBuffer); i += pSession->uMaximumMessageLength)
	{
		int len = estrLength(&pSession->estrCipherBuffer);
		bool last = i + pSession->uMaximumMessageLength >= (unsigned)len;
		if (last)
			estrForceSize(&pSession->estrCipherBuffer, len + pSession->uTrailerLength);
		EncryptTls(pSessionExternal, pSession->estrCipherBuffer + i, (unsigned long)MIN(len - i, pSession->uMaximumMessageLength), !last);
		if (last)
			estrForceSize(&pSession->estrCipherBuffer, len);
	}

	// Clear the cipher buffer.
	estrClear(&pSession->estrCipherBuffer);
}

// Start or continue TLS negotiation.
// Forward declaration for renegotiation from DecryptTls().
static void NegotiateTls(struct TlsSession *pSessionExternal, const char *pData, unsigned long uLength);

// Decrypt a TLS data block.
static void DecryptTls(struct TlsSession *pSessionExternal, const char *pData, unsigned long uLength)
{
	struct TlsSessionInternal *pSession = pSessionExternal->pInternal;
	SecBufferDesc messageDesc;
	SecBuffer message[4];
	int i;
	SECURITY_STATUS status;
	const SecBuffer *plaintext;
	const SecBuffer *extra;
	char debug[1024];

	devassert(pSession->uFlags & TlsSessionFlag_NegotiationComplete);
	devassert(!(pSession->uFlags & TlsSessionFlag_Ended));

	// Return if there is no data to decrypt.
	if (!uLength && !estrLength(&pSession->estrInBuffer))
		return;

	// Copy data to the input buffer.
	estrConcat(&pSession->estrInBuffer, pData, uLength);

	// Continue attempting to decrypt data until all data is exhausted, more data is needed to continue decryption, or there is a failure.
	while (estrLength(&pSession->estrInBuffer))
	{
		// Set up input buffer.
		message[0].BufferType = SECBUFFER_DATA;
		message[0].pvBuffer = pSession->estrInBuffer;
		message[0].cbBuffer = estrLength(&pSession->estrInBuffer);
		for (i = 1; i != 4; ++i)
		{
			message[i].BufferType = SECBUFFER_EMPTY;
			message[i].pvBuffer = NULL;
			message[i].cbBuffer = 0;
		}
		messageDesc.ulVersion = SECBUFFER_VERSION;
		messageDesc.cBuffers = sizeof(message)/sizeof(*message);
		messageDesc.pBuffers = message;
	
		// Try to decrypt the TLS data.
		status = sspi->DecryptMessage(&pSession->ctxtTlsContext, &messageDesc, 0, NULL);
	
		// Handle outcome of decryption attempt.
		switch (status)
		{
			case SEC_E_OK:
				break;
	
			// Not enough data to decrypt anything
			case SEC_E_INCOMPLETE_MESSAGE:
				return;
	
			// The peer wants to renegotiate.
			// Renegotiation will occur after the buffers are processed.
			case SEC_I_RENEGOTIATE:
				pSession->uFlags |= TlsSessionFlag_Renegotiate;
				break;
	
			// The peer has shut down the TLS session.
			case SEC_I_CONTEXT_EXPIRED:
				SessionComplete(pSessionExternal);
				return;
	
			// Invalid data received
			case SEC_E_DECRYPT_FAILURE:
			case SEC_E_MESSAGE_ALTERED:
			case SEC_E_ILLEGAL_MESSAGE:
			case SEC_E_UNSUPPORTED_FUNCTION:
			case SEC_E_ALGORITHM_MISMATCH:
				SessionFailed(pSessionExternal);
				return;

			// Internal runtime error
			case SEC_E_INTERNAL_ERROR:
				SessionFailed(pSessionExternal);
				Errorf("Internal error while decrypting TLS");
				return;
	
			// Logic error
			case SEC_E_INVALID_HANDLE:
			case SEC_E_OUT_OF_SEQUENCE:
				devassertmsgf(0, "DecryptMessage(): %lx", (unsigned long)status);
				SessionFailed(pSessionExternal);
				return;

			// "Invalid token" error
			// From the documentation, it looks like SEC_E_INVALID_TOKEN is supposed to be a logic error.  It says:
			// "SEC_E_INVALID_TOKEN: The buffers are of the wrong type or no buffer of type SECBUFFER_DATA was found. Used with the Schannel SSP."
			// We always set up the buffers the same way; the only differences are the size of the buffers and the contents of the buffers.  However,
			// we get this error occasionally on the live XmppServer, so it's not really clear what's going on.  We'll assume this is a case of the
			// documentation being inaccurate, but we'll log some information about it in case there's a real bug here.
			case SEC_E_INVALID_TOKEN:
				encodeBase64String(pSession->estrInBuffer, estrLength(&pSession->estrInBuffer) > 256 ? 256 : estrLength(&pSession->estrInBuffer),
					debug, sizeof(debug));
				log_printf(LOG_BUG, "TlsDebugInvalidToken(databuffersize %lu database64 \"%s\" flags %lu)",
					estrLength(&pSession->estrInBuffer), debug, pSession->uFlags);
				SessionFailed(pSessionExternal);
				return;
	
			default:
				WinErrorf(status, "Unknown SSPI error while decrypting TLS data");
				SessionFailed(pSessionExternal);
				return;
		}
	
		// Find output buffers.
		// The undocumented assumption is that there is only one buffer of each type, and all of the extra empty buffers don't do anything
		// important.
		plaintext = NULL;
		extra = NULL;
		for (i = 0; i != sizeof(message)/sizeof(*message); ++i)
		{
			if ((message[i].BufferType & ~SECBUFFER_ATTRMASK) == SECBUFFER_DATA && message[i].pvBuffer && message[i].cbBuffer)
			{
				devassert(!plaintext);
				plaintext = &message[i];
			}
			if ((message[i].BufferType & ~SECBUFFER_ATTRMASK) == SECBUFFER_EXTRA && message[i].pvBuffer && message[i].cbBuffer)
			{
				devassert(!extra);
				extra = &message[i];
			}
		}

		// Send decrypted data to the user callback.
		if (plaintext)
			pSession->fpPlainDataReceived(pSessionExternal, pSession->pUserData, plaintext->pvBuffer, plaintext->cbBuffer);

		// Save extra data in the input buffer.
		if (extra)
		{
			devassert(extra->cbBuffer <= estrLength(&pSession->estrInBuffer));
			memmove(pSession->estrInBuffer, extra->pvBuffer, extra->cbBuffer);
			estrForceSize(&pSession->estrInBuffer, extra->cbBuffer);
		}
		else
			estrClear(&pSession->estrInBuffer);

		// Renegotiate if the peer requested it.
		if (pSession->uFlags & TlsSessionFlag_Renegotiate)
		{
			NegotiateTls(pSessionExternal, NULL, 0);
			return;
		}
	}
}

// Encrypt or decrypt any pending data.
static void FinishNegotiation(struct TlsSession *pSessionExternal)
{
	struct TlsSessionInternal *pSession = pSessionExternal->pInternal;

	devassert(pSession->uFlags & TlsSessionFlag_NegotiationComplete);
	devassert(pSession->uFlags & TlsSessionFlag_SessionStarted);

	// Decrypt any pending ciphertext.
	if (pSession->uFlags & TlsSessionFlag_NegotiationComplete
		&& estrLength(&pSession->estrInBuffer))
		DecryptTls(pSessionExternal, NULL, 0);

	// Encrypt any pending data.
	if (pSession->uFlags & TlsSessionFlag_NegotiationComplete
		&& !(pSession->uFlags & TlsSessionFlag_Ended)
		&& estrLength(&pSession->estrCipherBuffer))
		EncryptFromBuffer(pSessionExternal);
}

// Start or continue TLS negotiation.
static void NegotiateTls(struct TlsSession *pSessionExternal, const char *pData, unsigned long uLength)
{
	struct TlsSessionInternal *pSession = pSessionExternal->pInternal;
	SecBuffer inBuffer[2];
	SecBufferDesc inBufferDesc;
	SecBuffer outBuffer;
	SecBufferDesc outBufferDesc;
	unsigned long attributes;
	SECURITY_STATUS status;

	devassert(pData && uLength || pSession->uFlags & TlsSessionFlag_Renegotiate);

	// Copy data to the input buffer.
	estrConcat(&pSession->estrInBuffer, pData, uLength);

	// Keep trying to negotiate with the data that we have until success, failure, or we realize we need more data.
	while (estrLength(&pSession->estrInBuffer))
	{
		bool firstPacket = !(pSession->uFlags & TlsSessionFlag_NegotiationStarted);
		bool renegotiating = pSession->uFlags & TlsSessionFlag_Renegotiate;

		// Determine negotiation state.
		devassert(!(pSession->uFlags & TlsSessionFlag_Ended));
		devassert(!renegotiating || pSession->uFlags & TlsSessionFlag_NegotiationComplete);
		if (renegotiating)
			pSession->uFlags &= ~(TlsSessionFlag_NegotiationComplete|TlsSessionFlag_Renegotiate);
		devassert(!(pSession->uFlags & TlsSessionFlag_NegotiationComplete));
		pSession->uFlags |= TlsSessionFlag_NegotiationStarted;
	
		// Set up input buffers.
		inBuffer[0].BufferType = SECBUFFER_TOKEN;
		inBuffer[0].pvBuffer = renegotiating ? NULL : pSession->estrInBuffer;
		inBuffer[0].cbBuffer = renegotiating ? 0 : estrLength(&pSession->estrInBuffer);
		inBuffer[1].BufferType = SECBUFFER_EMPTY;
		inBuffer[1].pvBuffer = NULL;
		inBuffer[1].cbBuffer = 0;
		inBufferDesc.ulVersion = SECBUFFER_VERSION;
		inBufferDesc.cBuffers = sizeof(inBuffer)/sizeof(*inBuffer);
		inBufferDesc.pBuffers = inBuffer;
	
		// Initialize output buffers.
	 	outBuffer.BufferType = SECBUFFER_TOKEN;
	 	outBuffer.pvBuffer = NULL;
	 	outBuffer.cbBuffer = 0;
	 	outBufferDesc.ulVersion = SECBUFFER_VERSION;
	 	outBufferDesc.cBuffers = 1;
	 	outBufferDesc.pBuffers = &outBuffer;
	
		// Attempt TLS negotiation.
		devassert(pSession->pCertificate->pInternal->bCredentialsAcquired);
		status = sspi->AcceptSecurityContext(&pSession->pCertificate->pInternal->credTlsCredentials,
			firstPacket ? NULL : &pSession->ctxtTlsContext,
			&inBufferDesc,
			ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_CONFIDENTIALITY | ASC_REQ_EXTENDED_ERROR | ASC_REQ_REPLAY_DETECT
				| ASC_REQ_SEQUENCE_DETECT | ASC_REQ_STREAM,
			SECURITY_NATIVE_DREP,
			&pSession->ctxtTlsContext,
			&outBufferDesc,
			&attributes, NULL);
	
		// Adjust the input buffer.
		// This could be allocating a new buffer for extra data, removing data from a partially-read buffer, or freeing a completely-read buffer.
		if (status == SEC_E_INCOMPLETE_MESSAGE)
		{
			// In this case, nothing should have happened, and we should just resend the entire thing when we get more data.  In particular,
			// a TLS context doesn't seem to be allocated, although the documentation is not clear here, and the PSDK sample is definitely
			// assuming that it is.
			// Hypothetically, we could use the SECBUFFER_MISSING buffer to optimize in the way the Windows SDK describes, but it doesn't
			// actually seem to be present in practice; until this situation is cleared up, we do not handle perform this optimization
			// in the interests of robustness.
			devassertmsgf(inBuffer[1].BufferType == SECBUFFER_EMPTY || inBuffer[1].BufferType == SECBUFFER_MISSING,
				"AcceptSecurityContext(): SEC_E_INCOMPLETE_MESSAGE, buffer %lx", inBuffer[1].BufferType);
		}
		else if ((inBuffer[1].BufferType & ~SECBUFFER_ATTRMASK) == SECBUFFER_EXTRA && inBuffer[1].pvBuffer && inBuffer[1].cbBuffer)
		{
			devassert(inBuffer[1].cbBuffer <= estrLength(&pSession->estrInBuffer));
			memmove(pSession->estrInBuffer, inBuffer[1].pvBuffer, inBuffer[1].cbBuffer);
			estrForceSize(&pSession->estrInBuffer, inBuffer[1].cbBuffer);
		}
		else if (!renegotiating)
		{
			// Note: This condition seems a little questionable.  Although I wrote this, it's not clear to me now why we do not clear
			// in the renegotiation case also.  But presumably removing the condition will break renegotiation some sort of way.
			estrDestroy(&pSession->estrInBuffer);
		}
	
		// Send reply to peer if necessary.
		if (outBuffer.pvBuffer && outBuffer.cbBuffer)
		{
			SECURITY_STATUS freeSuccess;
			pSession->fpCipherDataSend(pSessionExternal, pSession->pUserData, outBuffer.pvBuffer, outBuffer.cbBuffer);
			freeSuccess = sspi->FreeContextBuffer(outBuffer.pvBuffer);
			devassertmsgf(freeSuccess == SEC_E_OK, "FreeContextBuffer(): %lx", freeSuccess);
		}
	
		// Determine outcome of this negotiation attempt.
		switch (status)
		{
			// Negotiation has completed successfully.
			case SEC_E_OK:
				if (renegotiating)
					break;
				{
					bool initialSessionOpen = !(pSession->uFlags & TlsSessionFlag_SessionStarted);
					pSession->uFlags |= TlsSessionFlag_NegotiationComplete | TlsSessionFlag_SessionStarted;
					if (initialSessionOpen)
						pSession->fpOpened(pSessionExternal, pSession->pUserData);
					FinishNegotiation(pSessionExternal);
				}
				return;
	
			// The client needs to provide more data before negotiation can proceed; keep the buffer, and start all the way over.
			case SEC_E_INCOMPLETE_MESSAGE:

				// Reset the flags to effectively how they were before we were called.
				pSession->uFlags &= ~TlsSessionFlag_NegotiationStarted;
				if (renegotiating)
					pSession->uFlags |= TlsSessionFlag_Renegotiate;

				// Anecdotally, Schannel seems willing to accept an unbounded amount of malformed "hello" data without triggering an error,
				// so we check ourselves to make sure that this input data actually could potentially be valid.
				if (estrLength(&pSession->estrInBuffer) > sspiMaxToken)
					SessionFailed(pSessionExternal);
				return;

			// Negotiation will continue.
			case SEC_I_CONTINUE_NEEDED:
				break;
	
			// Negotiation error
			case SEC_E_INVALID_TOKEN:
			case SEC_E_ILLEGAL_MESSAGE:
			case SEC_E_DECRYPT_FAILURE:
			case SEC_E_UNSUPPORTED_FUNCTION:
			case SEC_E_ALGORITHM_MISMATCH:
			case SEC_E_CERT_UNKNOWN:
			case SEC_E_CERT_EXPIRED:
			case SEC_E_UNTRUSTED_ROOT:
			case SEC_E_LOGON_DENIED:
			case SEC_E_MESSAGE_ALTERED:									// Undocumented as of MSDN 7/14/2011, but observed in live production on XmppServer on Windows XP Pro x64 SP2, build 3790, on 2011-08-14.
			case SEC_I_CONTEXT_EXPIRED:									// Undocumented as of MSDN 7/14/2011, but observed in live production on XmppServer on Windows XP Pro x64 SP2, build 3790, on 2011-08-26.
				SessionFailed(pSessionExternal);
				return;
	
			// Internal runtime error
			case SEC_E_INTERNAL_ERROR:
				SessionFailed(pSessionExternal);
				Errorf("Internal error while negotiating TLS");
				return;
	
			// Out of memory
			case SEC_E_INSUFFICIENT_MEMORY:
				SessionFailed(pSessionExternal);
				Errorf("Out of memory while negotiating TLS");
				return;
	
			// Logic error
			case SEC_I_COMPLETE_AND_CONTINUE:
			case SEC_I_COMPLETE_NEEDED:
			case SEC_E_INVALID_HANDLE:
			case SEC_E_NO_AUTHENTICATING_AUTHORITY:
			case SEC_E_NO_CREDENTIALS:
				devassertmsgf(0, "AcceptSecurityContext(): %lx", (unsigned long)status);
				SessionFailed(pSessionExternal);
				return;
	
			// Unknown error
			default:
				WinErrorf(status, "Unknown SSPI error while negotiating TLS session");
				SessionFailed(pSessionExternal);
				return;
		}
	}
}

/************************************************************************/
/* External TLS Interface Functions                                     */
/************************************************************************/

// Load a private key certificate from our local store for a particular common name.
TlsCertificate *tlsLoadCertificate(const char *pCommonName)
{
	int err;
	const CERT_CONTEXT *context = NULL;
	HCERTSTORE store;
	char *name = NULL;
	TlsCertificate *cert;

	PERFINFO_AUTO_START_FUNC();

	// Verify parameters.
	if (!devassert(pCommonName))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	// Open the system private key store.
	store = CertOpenSystemStoreA(0, "MY");
	if (!store)
	{
		WinErrorf(GetLastError(), "CertOpenSystemStore()");
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	// Find a matching certificate.
	estrStackCreate(&name);
	estrForceSize(&name, 256);
	while ((context = CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR_A, pCommonName, context)))
	{
		DWORD len;
		const char *begin;
		const char *end;
		const char *slash;
		size_t nameLen;

		// Get the X500 name of the subject.
		if (!context->pCertInfo)
			continue;
		len = tlsCertNameToStrAlloc(X509_ASN_ENCODING, &context->pCertInfo->Subject, CERT_X500_NAME_STR, &name);
		if (!len)
		{
			WinErrorf(GetLastError(), "CertFindCertificateInStore()");
			continue;
		}

		// Find the beginning of the CN key.
		begin = FindCN(name);
		if (!begin)
			continue;

		// Find the end of the CN key.
		end = strchr(begin, ',');
		slash = strchr(begin, '/');
		if (slash && (!end || slash < end))
			end = slash;
		while (end && end != begin && *end == ' ')
			--end;
		if (end)
			nameLen = end - begin;
		else
			nameLen = strlen(begin);

		// Check if this is the certificate we're after.
		if (!strnicmp(begin, pCommonName, nameLen))
			break;																				// Break on success.
	}
	err = GetLastError();
	estrDestroy(&name);
	if (!context && err != CRYPT_E_NOT_FOUND)
	{
		WinErrorf(GetLastError(), "CertFindCertificateInStore()");
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}
	if (!context)
	{
		Errorf("Certificate \"%s\" not found", pCommonName);
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	// Print certificate information for debugging.
	if (tlsDebugLevel() >= 1 && context && context->pCertInfo)
	{
		unsigned long i;
		char *subjectName = NULL;
		DWORD len;
		estrStackCreate(&subjectName);
		printf("Loaded certificate: ");
		printf("Version = \"%lu\"", context->pCertInfo->dwVersion);
		estrForceSize(&subjectName, 256);
		len = tlsCertNameToStrAlloc(X509_ASN_ENCODING, &context->pCertInfo->Subject, CERT_X500_NAME_STR, &subjectName);
		if (len)
			printf(", Subject = \"%s\"", subjectName);
		if (tlsDebugLevel() >= 2)
		{
			printf(", Serial = \"");
			for (i = 0; i != context->pCertInfo->SerialNumber.cbData; ++i)
				printf("%02x", (int)context->pCertInfo->SerialNumber.pbData[i]);
			putchar('"');
		}
		putchar('\n');
		estrDestroy(&subjectName);
	}

	// Create certificate structure.
	cert = malloc(sizeof(TlsCertificate));
	cert->pInternal = malloc(sizeof(struct TlsCertificateInternal));
	cert->pInternal->pContext = context;
	cert->pInternal->bCredentialsAcquired = false;
	cert->pInternal->uRefCount = 0;

	// Check the certificate's validity.
	CheckCertificateValidity(cert);

	PERFINFO_AUTO_STOP_FUNC();

	return cert;
}

// Free a certificate loaded by tlsLoadCertificate().
void tlsFreeCertificate(TlsCertificate *pCertificate)
{
	bool success;

	PERFINFO_AUTO_START_FUNC();

	// Do nothing if null.
	if (!pCertificate)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Make sure no references remain.
	if (pCertificate->pInternal->uRefCount)
	{
		PERFINFO_AUTO_STOP_FUNC();
		Errorf("This certificate still has %"FORM_LL"u references", pCertificate->pInternal->uRefCount);
		return;
	}

	// Free the internal members.
	success = CertFreeCertificateContext(pCertificate->pInternal->pContext);
	if (!success)
		WinErrorf(GetLastError(), "CertFreeCertificateContext()");

	// Free the memory.
	free(pCertificate->pInternal);
	free(pCertificate);

	PERFINFO_AUTO_STOP_FUNC();
}

// Start server-side TLS.
TlsSession *tlsSessionStartServer(void *pUserData, TlsCertificate *pCertificate, TlsSessionDataCallback fpPlainDataReceived,
						   TlsSessionDataCallback fpCipherDataSend, TlsSessionNotifyCallback fpOpened, TlsSessionNotifyCallback fpClosed)
{

	TlsSession *session;
	SECURITY_STATUS status;
	SCHANNEL_CRED cred;
	const CERT_CONTEXT *contextArray;

	PERFINFO_AUTO_START_FUNC();

	// Verify parameters.
	if (!pCertificate)
	{
		Errorf("Server-side TLS requires a certificate.");
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}
	if (!devassert(pCertificate->pInternal && pCertificate->pInternal->pContext))
	{
		Errorf("Certificate internal data missing");
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}
	if (!fpPlainDataReceived && fpCipherDataSend && fpOpened && fpClosed)
	{
		Errorf("All callbacks must be specified.");
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	// Initialize Schannel.
	InitializeSchannelIfNecessary();
	if (!schannelInitialized)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	// Check certificate validity.
	CheckCertificateValidity(pCertificate);

	// Allocate session object.
	session = malloc(sizeof(TlsSession));
	session->pInternal = malloc(sizeof(struct TlsSessionInternal));
	session->pInternal->pUserData = pUserData;
	session->pInternal->pCertificate = pCertificate;
	session->pInternal->fpPlainDataReceived = fpPlainDataReceived;
	session->pInternal->fpCipherDataSend = fpCipherDataSend;
	session->pInternal->fpOpened = fpOpened;
	session->pInternal->fpClosed = fpClosed;
	session->pInternal->uFlags = 0;
	session->pInternal->estrInBuffer = NULL;
	session->pInternal->estrCipherBuffer = NULL;
	session->pInternal->uCipherBufferLimit = 1024*1024;  // Default is one megabyte.
	session->pInternal->uMaximumMessageLength = 0;
	session->pInternal->iInUse = 0;
	tlsEnter(session);

	// Initialize TLS context.
	// Note: This is not required by the API, and is somewhat improper because it makes assumptions
	// about the format of SecHandles, which is undocumented.  In my experimentation, arbitrary random
	// handle values typically cause crashes inside the security provider, while this zero value
	// seems to always cause an error return of SEC_E_INVALID_HANDLE.  The purpose of setting this
	// here is so that accidental uses of uninitialized handles will not crash in production; note
	// however that they should cause a soft assert after return.  The motivation for doing this is
	// that AcceptSecurityContext() does not always seem to initialize the context in situations
	// where the documentation and example code leads us to believe it should.  If you want to make
	// this condition crash immediately, for instance when debugging, change the 0 values to something
	// else.
	// Please note that there is a similar assignment in SessionComplete().
	session->pInternal->ctxtTlsContext.dwLower = 0; // = 0xdeadbeef;
	session->pInternal->ctxtTlsContext.dwUpper = 0; // = 0xbaadf00d;

	// Load credentials if needed.
	// This includes the certificate, the private key, cipher choices, etc.
	if (!pCertificate->pInternal->bCredentialsAcquired)
	{

		// Set up credential options.
		contextArray = pCertificate->pInternal->pContext;
		cred.dwVersion = SCHANNEL_CRED_VERSION;
		cred.cCreds = 1;
		cred.paCred = &contextArray;
		cred.hRootStore = NULL;
		cred.cMappers = 0;
		cred.aphMappers = NULL;
		cred.cSupportedAlgs = 0;
		cred.palgSupportedAlgs = NULL;  // Note: The server needs to have insecure algorithms disabled in the registry.
		cred.grbitEnabledProtocols = SP_PROT_SSL3_SERVER | SP_PROT_TLS1_SERVER;
		cred.dwMinimumCipherStrength = 128;
		cred.dwMaximumCipherStrength = 0;
		cred.dwSessionLifespan = 0;
		cred.dwFlags = SCH_CRED_NO_SYSTEM_MAPPER;
		cred.dwCredFormat = 0;
	
		// Attempt to obtain credentials.
		status = sspi->AcquireCredentialsHandle(NULL, UNISP_NAME, SECPKG_CRED_INBOUND, NULL, &cred, NULL, NULL,
			&pCertificate->pInternal->credTlsCredentials, NULL);
		if (status != SEC_E_OK)
		{
			WinErrorf(status, "AcquireCredentialsHandle()");
			free(session->pInternal);
			free(session);
			PERFINFO_AUTO_STOP_FUNC();
			return NULL;
		}
		pCertificate->pInternal->bCredentialsAcquired = true;
	}

	// Increment certificate reference count.
	++pCertificate->pInternal->uRefCount;
	tlsLeave(session);

	PERFINFO_AUTO_STOP_FUNC();

	return session;
}

// Begin graceful shutdown of a TLS session.
void tlsSessionShutdown(TlsSession *pSession)
{
	struct TlsSessionInternal *pInternal;
	SECURITY_STATUS status;
	DWORD token;
	SecBuffer tokenBuffer;
	SecBufferDesc tokenBufferDesc;
	SecBuffer inBuffer[2];
	SecBufferDesc inBufferDesc;
	SecBuffer outBuffer;
	SecBufferDesc outBufferDesc;
	unsigned long attributes;

	PERFINFO_AUTO_START_FUNC();

	// Verify parameters.
	if (!pSession)
	{
		Errorf("No session");
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	pInternal = pSession->pInternal;
	if (!(pInternal->uFlags & TlsSessionFlag_Ended))
	{
		Errorf("Session already closed");
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	tlsEnter(pSession);

	// Create control token.
	token = SCHANNEL_SHUTDOWN;
	tokenBuffer.BufferType = SECBUFFER_TOKEN;
	tokenBuffer.pvBuffer = &token;
	tokenBuffer.cbBuffer = sizeof(token);
	tokenBufferDesc.ulVersion = SECBUFFER_VERSION;
	tokenBufferDesc.cBuffers = 1;
	tokenBufferDesc.pBuffers = &tokenBuffer;

	// Shut down context.
	status = ApplyControlToken(&pInternal->ctxtTlsContext, &inBufferDesc);
	if (status != SEC_E_OK)
	{
		devassert(status != SEC_E_UNSUPPORTED_FUNCTION);
		WinErrorf(status, "Unable to shut down stream");
		SessionFailed(pSession);
		tlsLeave(pSession);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Set up dummy buffers.
	inBuffer[0].BufferType = SECBUFFER_TOKEN;
	inBuffer[0].pvBuffer = NULL;
	inBuffer[0].cbBuffer = 0;
	inBuffer[1].BufferType = SECBUFFER_EMPTY;
	inBuffer[1].pvBuffer = NULL;
	inBuffer[1].cbBuffer = 0;
	inBufferDesc.ulVersion = SECBUFFER_VERSION;
	inBufferDesc.cBuffers = sizeof(inBuffer)/sizeof(*inBuffer);
	inBufferDesc.pBuffers = inBuffer;
	outBuffer.BufferType = SECBUFFER_TOKEN;
	outBuffer.pvBuffer = NULL;
	outBuffer.cbBuffer = 0;
	outBufferDesc.ulVersion = SECBUFFER_VERSION;
	outBufferDesc.cBuffers = 1;
	outBufferDesc.pBuffers = &outBuffer;

	// Generate close notification to peer.
	devassert(pSession->pInternal->pCertificate->pInternal->bCredentialsAcquired);
	status = sspi->AcceptSecurityContext(&pSession->pInternal->pCertificate->pInternal->credTlsCredentials,
		&pInternal->ctxtTlsContext,
		&inBufferDesc,
		ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_CONFIDENTIALITY | ASC_REQ_EXTENDED_ERROR | ASC_REQ_REPLAY_DETECT
			| ASC_REQ_SEQUENCE_DETECT | ASC_REQ_STREAM,
		SECURITY_NATIVE_DREP,
		&pInternal->ctxtTlsContext,
		&outBufferDesc,
		&attributes,
		NULL);
	if (status != SEC_E_OK)
	{
		WinErrorf(status, "Unable to send close notification");
		SessionFailed(pSession);
		tlsLeave(pSession);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Send close notification to peer.
	devassert(outBuffer.pvBuffer && outBuffer.cbBuffer);
	pInternal->fpCipherDataSend(pSession, pInternal->pUserData, outBuffer.pvBuffer, outBuffer.cbBuffer);
	status = sspi->FreeContextBuffer(outBuffer.pvBuffer);
	devassertmsgf(status == SEC_E_OK, "FreeContextBuffer() %lx", (unsigned long)status);
	tlsLeave(pSession);

	PERFINFO_AUTO_STOP_FUNC();
}

// Close a TLS session and free any resources associated with it.
void tlsSessionDestroy(TlsSession *pSession)
{
	PERFINFO_AUTO_START_FUNC();

	// Return if nothing to do.
	if (!pSession)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Free any resources.
	tlsEnter(pSession);
	if (pSession->pInternal->uFlags & TlsSessionFlag_NegotiationStarted
		&& !(pSession->pInternal->uFlags & TlsSessionFlag_Ended))
	{
		SECURITY_STATUS status = DeleteSecurityContext(&pSession->pInternal->ctxtTlsContext);
		devassertmsgf(status == SEC_E_OK, "DeleteSecurityContext() %lx", (unsigned long)status);
	}
	--pSession->pInternal->pCertificate->pInternal->uRefCount;
	estrDestroy(&pSession->pInternal->estrCipherBuffer);
	estrDestroy(&pSession->pInternal->estrInBuffer);
	free(pSession->pInternal);
	free(pSession);

	PERFINFO_AUTO_STOP_FUNC();
}

// Send this plaintext data over the TLS link.
void tlsSessionSendPlaintext(TlsSession *pSession, const char *pData, size_t uLength)
{
	struct TlsSessionInternal *pInternal = pSession->pInternal;
	SECURITY_STATUS status;

	PERFINFO_AUTO_START_FUNC();

	// Verify parameters.
	if (!pData || !uLength)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	if (!pSession)
	{
		Errorf("No session");
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	devassert(pInternal);

	// Do nothing if this session is not active.
	if (pInternal->uFlags & TlsSessionFlag_Ended)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Allow one level of reentrancy when encrypting.
	devassertmsgf(pInternal->iInUse < 2, "Only one level of reentrancy is allowed when encrypting (%d)", pInternal->iInUse);
	++pInternal->iInUse;

	// Get packet size information.
	if (!pInternal->uMaximumMessageLength)
	{
		SecPkgContext_StreamSizes sizes;
		status = QueryContextAttributes(&pInternal->ctxtTlsContext, SECPKG_ATTR_STREAM_SIZES, &sizes);
		if (status != SEC_E_OK)
		{
			WinErrorf(status, "Unable to get SSPI context stream sizes");
			SessionFailed(pSession);
			--pInternal->iInUse;
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
		pInternal->uHeaderLength = sizes.cbHeader;
		devassert(pInternal->uHeaderLength);
		pInternal->uTrailerLength = sizes.cbTrailer;
		devassert(pInternal->uTrailerLength);
		pInternal->uMaximumMessageLength = sizes.cbMaximumMessage;
		devassert(pInternal->uMaximumMessageLength);
	}

	// If negotiation is not complete, leave the data in the buffer and return.
	if (!(pInternal->uFlags & TlsSessionFlag_NegotiationComplete))
	{
		int len = estrLength(&pInternal->estrCipherBuffer) ? estrLength(&pInternal->estrCipherBuffer) : pInternal->uHeaderLength;
		if (len + uLength - pInternal->uHeaderLength > pInternal->uCipherBufferLimit)
		{
			SessionFailed(pSession);
			--pInternal->iInUse;
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
		if (!estrLength(&pInternal->estrCipherBuffer))
			estrForceSize(&pInternal->estrCipherBuffer, pInternal->uHeaderLength);
		devassert(estrLength(&pInternal->estrCipherBuffer) >= pInternal->uHeaderLength);
		estrConcat(&pInternal->estrCipherBuffer, pData, (int)uLength);
		--pInternal->iInUse;
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Encrypt the data.
	if (!estrLength(&pInternal->estrCipherBuffer))
		estrForceSize(&pInternal->estrCipherBuffer, pInternal->uHeaderLength);
	devassert(estrLength(&pInternal->estrCipherBuffer) >= pInternal->uHeaderLength);
	estrConcat(&pInternal->estrCipherBuffer, pData, (int)uLength);
	EncryptFromBuffer(pSession);
	--pInternal->iInUse;

	PERFINFO_AUTO_STOP_FUNC();
}

// Ciphertext was received over the TLS link, and needs to be decrypted.
void tlsSessionReceivedCiphertext(TlsSession *pSession, const char *pData, size_t uLength)
{
	size_t i;

	PERFINFO_AUTO_START_FUNC();

	// Verify parameters.
	if (!pData || !uLength)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	if (!pSession)
	{
		Errorf("No session");
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	devassert(pSession->pInternal);

	// Do nothing if this session is not active.
	if (pSession->pInternal->uFlags & TlsSessionFlag_Ended)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// If negotiation is incomplete, negotiate.  Otherwise, decrypt.
	// The strange loop is due to SSPI not supporting buffer sizes larger than an unsigned long.
	tlsEnter(pSession);
	for (i = 0; i < uLength; i += ULONG_MAX)
	{
		if (pSession->pInternal->uFlags & TlsSessionFlag_NegotiationComplete)
			DecryptTls(pSession, pData + i, (unsigned long)MIN((size_t)ULONG_MAX, uLength - i));
		else
			NegotiateTls(pSession, pData + i, (unsigned long)MIN((size_t)ULONG_MAX, uLength - i));
	}
	tlsLeave(pSession);

	PERFINFO_AUTO_STOP_FUNC();
}

// Set the maximum incoming buffer size for a TLS session.
void tlsSessionSetBufferLimit(TlsSession *pSession, size_t uSize)
{
	PERFINFO_AUTO_START_FUNC();

	if (!pSession)
	{
		Errorf("No session");
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	tlsEnter(pSession);

	// Set the limit.
	pSession->pInternal->uCipherBufferLimit = MIN(uSize, INT_MAX);

	tlsLeave(pSession);

	PERFINFO_AUTO_STOP_FUNC();
}
