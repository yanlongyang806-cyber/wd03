/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct CCGPlayerData CCGPlayerData;
typedef struct CCGAttribute CCGAttribute;

//
// Cards are identified by a 32 bit unique identifier
//
// The lower 24 bits are the card number.  The card number
//  uniquely identifies the card for the purposes of gameplay.
//  This is the number that the designers use to identify a card.
// The upper 8 bits are the flags.  Currently only three are
//  defined.  They are:
//  CCG_CARD_FLAG_TRADEABLE - if true the card can be traded to other players
//  CCG_CARD_FLAG_FOIL - if true the card will have some special visual effect
//  CCG_CARD_FLAG_DEBUG - if true the card was created by a developer for testing
//
// Custom cards have the high bit of the card number field
//  set.  The lower 23 bits of a custom card id are the custom
//  card number.  This number can be used to index the custom
//  card array.  
// NOTE - can custom cards be deleted or traded?  Probably not for
//  Champions.  Not sure about Star Trek.
//

#define CCG_CARD_NUM_INVALID 0
#define CCG_CARD_NUM_MASK 0xffffff
#define CCG_CARD_FLAG_TRADEABLE (1<<24)
#define CCG_CARD_FLAG_FOIL (1<<25)
#define CCG_CARD_FLAG_DEBUG (1<<26)

// Note that the custom card flag is part of the CardNum field.
// This is meant to keep custom cards from having CardNum values
//  that conflict with the standard cards.
#define CCG_CARD_FLAG_CUSTOM (1<<23)

// macros to extract the number and flags from a card id
#define CCG_GetCardNum(id) ((id)&CCG_CARD_NUM_MASK)

// extract the custom card number
#define CCG_GetCustomCardNum(id) ((id)&(CCG_CARD_NUM_MASK^CCG_CARD_FLAG_CUSTOM))

// card must have tradeable flag and not be debug in order to be traded
#define CCG_IsCardTradeable(id) (((id)&(CCG_CARD_FLAG_TRADEABLE|CCG_CARD_FLAG_DEBUG))==CCG_CARD_FLAG_TRADEABLE)

#define CCG_IsCardFoil(id) ((id)&CCG_CARD_FLAG_FOIL)

#define CCG_IsCardDebug(id) ((id)&CCG_CARD_FLAG_DEBUG)

#define CCG_IsCardCustom(id) ((id)&CCG_CARD_FLAG_CUSTOM)


