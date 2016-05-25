
nil subclass: Object [
	"testing functionality"
	class [ >-asm ldlc 0; clsof< ]
	isKindOf: aClass [ ^ (self isMemberOf: aClass) || (self class isSubclassOf: aClass) ]
	isMemberOf: aClass [ ^ self class == aClass ]
	respondsTo: aSymbol [ "TODO: implmentation in ASM or in VM?"  ]

	"comparing"
	== anObject [ >-asm ldlc 0; ldlc 1; cmp eql< ]
	= anObject [ ^ self == anObject ]
	~= anObject [ ^ (self = anObject) not ]
	~~ anObject [ ^ (self == anObject) not ]
	hash [ >-asm ldlc 0; hashof< ] "TODO: this is a bit silly to put hashing in the VM"

	"testing"
	isNil [ ^ false ]
	notNil [ ^ true ]	

	"copying"
	copy [ ^ self shallowCopy ]
	shallowCopy [ >-asm ldlc 0; copy shallow< ]
	deepCopy [ >-asm lclc 0; copy deep< ]
	
	"accessing"
	basicAt: index [ >-asm ldlc 1; ldinst $stack< ] 
	basicAt: index put: anObject [ >-asm ldlc 2; ldlc 1; mvinst $stack< ] 
	basicSize [ >-asm ldlc 0; %%%%%%magic size instruction%%%%%%< ] 
	at: index [ ^ self basicAt: index ] 
	at: index put: anObject [ ^ self basicAt: index put: anObject ]
	size [ ^ self basicSize ]

	"error handling"
	doesNotUnderstand: aMessage [ self error: (aMessage selector stringValue) ]
	error: aString [ >-asm ldlc 1; error< ]
	shouldNotImplement [ self error: 'not implemented' ]
	subclassResponsibility [ self error: 'subclass responsibility' ]
]

Object subclass: Magnitude [
	"comparing"
	< aMagnitude [ ]
	<= aMagnitude [ ]
	> aMagnitude [ ]
	>= aMagnitude [ ]
	between: min and: max [ ^ ( ]
	= aObject [ self subclassResponsibility ]
	
	"testing"
	min: aMagnitude [ ]
	max: aMagnitude [ ]
]

