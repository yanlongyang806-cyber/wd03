#pragma once

typedef struct UrlArgumentList UrlArgumentList;
typedef enum HttpMethod HttpMethod;

void oauthSign(UrlArgumentList *args, HttpMethod method, SA_PARAM_NN_STR const char *consumer_key, SA_PARAM_NN_STR const char *consumer_secret, 
			   const char *token, const char *secret);