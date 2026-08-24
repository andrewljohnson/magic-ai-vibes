# Composite objects and meld

Status: future design. No supported format currently needs meld, and the engine
does not execute it.

The identity model avoids assuming that every game object is backed by exactly
one card. A future meld action can consume two zone objects with one physical
backing each and create one battlefield object whose backing contains both
cards. If that permanent later changes zones, the result can be two new card
objects rather than forcing a false one-object/one-card mapping.

Finding the objects named by a meld ability and successfully melding their
physical cards are deliberately different operations. Name conditions inspect
the objects' effective characteristics. A Clone or token copy named Graf Rats
can therefore satisfy Midnight Scavengers' condition. Resolution first
performs the instructed exile zone changes, then the meld attempt validates
that the resulting objects are backed by the two complementary physical meld
cards. With a real Midnight Scavengers and only a copy of Graf Rats, that
validation fails; it does not undo the exile. A physical copy card remains in
exile, while a token follows the normal rule that makes it cease to exist.

`MeldRecipeDef` makes that boundary explicit in catalog data: each component
has a `required_name` for the object-level condition and a separate
`required_card` for physical-backing validation, while `MeldResultDef` owns the
combined object's name and rules instead of pretending it is either component.

This is the same general boundary used elsewhere: characteristic predicates
look at what an object currently is, while structural actions inspect what can
physically represent the requested result.
