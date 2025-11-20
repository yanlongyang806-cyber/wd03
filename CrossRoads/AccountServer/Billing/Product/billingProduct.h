#ifndef BILLINGPRODUCT_H
#define BILLINGPRODUCT_H

typedef struct BillingTransaction BillingTransaction;
typedef struct ProductContainer ProductContainer;

// Push = "to Vindicia"
// Pull = "from Vindicia"

BillingTransaction * btProductPush(const ProductContainer *pProduct);

#endif
