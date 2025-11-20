#ifndef EXPRESSIONPARSE_H
#define EXPRESSIONPARSE_H

int exprParseInternal(MultiVal*** dst, MultiVal*** src, Expression* expr);
void exprPostParseFixup(MultiVal*** vals);

#endif