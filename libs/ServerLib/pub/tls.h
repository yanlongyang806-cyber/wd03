// Support for TLS/SSL and x509 certificates

// Note that, at present, only server-side TLS is supported.
// See tlsCommands.c for a simple echo server example, or XMPP_Gateway.c for a more complicated STARTTLS example.

#ifndef CRYPTIC_TLS_H
#define CRYPTIC_TLS_H

/************************************************************************/
/* TLS Structures                                                       */
/************************************************************************/

// Internal structures
struct TlsCertificateInternal;
struct TlsSessionInternal;

// TLS certificate
typedef struct TlsCertificate
{
	struct TlsCertificateInternal *pInternal;
} TlsCertificate;

// TLS session
typedef struct TlsSession
{
	struct TlsSessionInternal *pInternal;
} TlsSession;


/************************************************************************/
/* TLS Activity Callbacks                                               */
/************************************************************************/

// This is called when data was received and decrypted, or data needs to be sent.
typedef void (*TlsSessionDataCallback)(TlsSession *pSession, void *pUserData, const char *pData, size_t uLength);

// This is called when a new session was successfully created or closed.
typedef void (*TlsSessionNotifyCallback)(TlsSession *pSession, void *pUserData);

/************************************************************************/
/* TLS Functions                                                        */
/************************************************************************/

// Load a private key certificate from our local store for a particular common name.
TlsCertificate *tlsLoadCertificate(const char *pCommonName);

// Runs the Certificate Validity check; AssertOrAlert if invalid or expired, ErrorOrAlert if certificate will expire soon (within 45 days)
void CheckCertificateValidity(TlsCertificate *pCert);

// Free a certificate loaded by tlsLoadCertificate().
void tlsFreeCertificate(TlsCertificate *pCertificate);

// Start server-side TLS.
TlsSession *tlsSessionStartServer(void *pUserData, TlsCertificate *pCertificate, TlsSessionDataCallback fpPlainDataReceived,
						   TlsSessionDataCallback fpCipherDataSend, TlsSessionNotifyCallback fpOpened, TlsSessionNotifyCallback fpClosed);

// Begin graceful shutdown of a TLS session.
void tlsSessionShutdown(TlsSession *pSession);

// Close a TLS session and free any resources associated with it.
void tlsSessionDestroy(TlsSession *pSession);

// Send this plaintext data over the TLS link.
void tlsSessionSendPlaintext(TlsSession *pSession, const char *pData, size_t uLength);

// Ciphertext was received over the TLS link, and needs to be decrypted.
void tlsSessionReceivedCiphertext(TlsSession *pSession, const char *pData, size_t uLength);

// Set the maximum incoming buffer size for a TLS session.
// This buffer is used when the user requests data be sent, but the session is still negotiating, or being renegotiated.
// Note that there is a fairly large hard upper limit on what this can be set to, and if a limit is requested that is larger than that,
// it will be silently decreased to the hard limit.
void tlsSessionSetBufferLimit(TlsSession *pSession, size_t uSize);

#endif  // CRYPTIC_TLS_H
