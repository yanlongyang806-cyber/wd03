typedef struct Entity Entity;

typedef enum {
	kEntityResolve_Success,
	kEntityResolve_NotFound,
	kEntityResolve_Ambiguous,
} EntityResolveReturn;

// Look for an player Entity ID that best matches the pchLookupName.
// PchLookupName should contain the character name and/or handle of
// the player we want to find.  
//
// This returns kEntityResolve_Success if a match was found.
// The entity ID of the match is placed in pResolvedEntId and
// the full name of the matched entity is placed in ppchResolvedFullName.
//
// If only account name is provided in pchLookupName (i.e. '@foobar'),
// then only the account will be used for matching.  This means
// if pchLookupName = 'bob@foobar', you could end up getting 
// 'larry@foobar' returned.
//
// If both the character and account name are provided in pchLookupName 
// (i.e. 'larry@foobar'), the this will only succeed if an exact match
// is found.
//
// If no account name is provided, then the character name will
// be used to match and will only return a match if it's completely
// unique across all of the 'known' entities.
//
// It's possible that the returned entity ID be 0 even with success.
// This may occur if the player is currently off line, but a non-zero
// result does not guarantee that the player is off line.  That depends on 
// where the information was found, which the caller as no control over.
//
// The ppchResolvedFullName will be filled in as best as possible from
// known information.  The handle is always provided, but it's possible
// that the character name may not be known at the time this is called.
// This may occur if the character is currently off line.  Again, this 
// depends on where the information was found, which the caller has no
// control over.
//
// This function looks for matches in the following locations:
//   Teammates
//   Friends
//   Guild Members
//   Recent receivers of pRequestor's chat (CLIENT ONLY)
//   Recent senders to pRequestor's chat (CLIENT ONLY)
//   Nearby Entities*
//
// IMPORTANT: The client has a slightly broader definition of "nearby entities" 
// than the server.  For the client, it's all the entities the client 
// is aware of, which is everyone nearby plus entities that have yet
// to be flushed from the client cache.  On the server side, it's strictly
// entities within a certain range of the requestor.
//
// IMPORTANT: Due to the above, it's generally more useful to run this on the client
// unless you absolutely can't because the definition of 'well-known' 
// entities encompasses a larger set.
// 
// The notify variant generates a notification if match is not found
// or is ambiguous.
EntityResolveReturn ResolveKnownEntityID(Entity *pRequestor, const char *pchLookupName, char **ppchResolvedFullName, char **ppchResolvedAccountName, U32 *pResolvedEntId);
EntityResolveReturn ResolveKnownEntityIDNotify(Entity *pRequestor, const char *pchLookupName, char **ppchResolvedFullName, char **ppchResolvedAccountName, U32 *pResolvedEntId);

// Similar to ResolveKnownEntityID() except that if an account
// is provided in the lookup name it just returns that in
// the ppchResolvedFullName.
// 
// The notify variant generates a notification if match is not found
// or is ambiguous.
EntityResolveReturn ResolveKnownAccount(Entity *pRequestor, const char *pchLookupName, char **ppchResolvedFullName, char **ppchResolvedAccountName);
EntityResolveReturn ResolveKnownAccountNotify(Entity *pRequestor, const char *pchLookupName, char **ppchResolvedFullName, char **ppchResolvedAccountName);

// Similar to ResolveKnownEntityID() except that if an account
// is provided in the lookup name it also returns the player's
// account ID.
// 
// The notify variant generates a notification if match is not found
// or is ambiguous.
EntityResolveReturn ResolveKnownAccountID(Entity *pRequestor, const char *pchLookupName, char **ppchResolvedFullName, char **ppchResolvedAccountName, U32 *pResolvedEntId, U32 *pResolvedAccountID, U32 *puiLoginServerID);
EntityResolveReturn ResolveKnownAccountIDNotify(Entity *pRequestor, const char *pchLookupName, char **ppchResolvedFullName, char **ppchResolvedAccountName, U32 *pResolvedEntId, U32 *pResolvedAccountID);

// Similar to ResolveKnownEntityID().  If a non-zero entity ID 
// is found, the Entity for that ID will be returned.
// 
// The notify variant generates a notification if match is not found
// or is ambiguous.
Entity *ResolveKnownEntity(Entity *pRequestor, const char *pchLookupName);
Entity *ResolveKnownEntityNotify(Entity *pRequestor, const char *pchLookupName);

// Similar to ResolveKnownAccountID() except that it returns an account ID
// and account name.  If the lookup name IS NOT KNOWN, then the returned 
// account ID will be 0 and the account name will be either an empty string
// or the account portion of the lookup name. 
//
// Returns true if either an account ID or account name is returned.
//
// Sends a notification if the lookup name is ambiguous or the known lookup
// failed and the lookup name does not have an account name (i.e. not found error).
bool ResolveNameOrAccountIDNotify(Entity *pEnt, const char *pchLookupName, char **ppchAccountName, U32 *piAccountId);
