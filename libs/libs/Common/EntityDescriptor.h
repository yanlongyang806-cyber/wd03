#ifndef ENTITY_DESCRIPTOR_H
#define ENTITY_DESCRIPTOR_H

AUTO_STRUCT AST_CONTAINER;
typedef struct EntityDescriptor
{
	const U32 uID; AST(KEY PERSIST)
	CONST_STRING_MODIFIABLE pEntityPTIStr; AST(NAME(EntityPTIStr) PERSIST)
	const U32 aiUniqueHash[4];    AST(PERSIST) // MD5 hash, generated from pEntityPTIStr
} EntityDescriptor;

AUTO_STRUCT;
typedef struct EntityDescriptorList
{
	EntityDescriptor **ppEntities;
	U32 uNextEntityID;
} EntityDescriptorList;

void loadEntityDescriptorList (EntityDescriptorList *pList);
EntityDescriptorList *getEntityDescriptorList(void);

EntityDescriptor * findEntityDescriptorByID(U32 uID);
U32 addEntityDescriptor(char *pEntityPTIStr);

bool loadParseTableAndStruct (SA_PARAM_NN_VALID ParseTable ***pti, void ** pData, char **estrName,
							  U32 uDescriptorID, const char *pStructStr);
void getEntityDescriptorName (SA_PARAM_NN_VALID char **estr, U32 uDescriptorID);

void destroyParseTableAndStruct (SA_PARAM_NN_VALID ParseTable ***pti, void ** pData);

bool entitySearchForSubstring(const char * pEntityStr, const char *pKey);


void setEntityDescriptorContainers(bool bIsUsing);
// Import from list into containers
void importEntityDescriptors (EntityDescriptorList *pList);

#endif