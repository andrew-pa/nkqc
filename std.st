
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
	< aMagnitude [ self subclassResponsibility ]
	<= aMagnitude [ self subclassResponsibility ]
	> aMagnitude [ self subclassResponsibility ]
	>= aMagnitude [ self subclassResponsibility ]
	between: min and: max [ ^ (self >= min) && (self <= max) ]
	= aObject [ self subclassResponsibility ]
	
	"testing"
	min: aMagnitude [ ^ (self < aMagnitude) ifTrue: [ self ] ifFalse: [ aMagnitude ] ]
	max: aMagnitude [ ^ (self > aMagnitude) ifTrue: [ self ] ifFalse: [ aMagnitude ] ]
]

Magnitude subclass: Character [
	| int_value |

	"instance creation"
	!static value: anInteger [ >-asm ldlc 1; special_value characer_for_int< ] 
	!static digitValue: anInteger [ ^ Character value: ((anInteger < 10) ifTrue: [ anInteger+30 ] ifFalse: [ anInteger+55 ]) ]  

	"accessing"
	asciiValue [ ^ int_value ]
	digitValue [ ^ (int_value > 39) ifTrue: [ int_value-55 ] ifFalse: [ int_value-30 ] ] 

	"testing"
	isDigit [ ^ int_value between: 10 and: 39 ]
	isLowercase [ ^ (int_value between 97 and 122) ]
	isUppercase [ ^ (int value between 65 and: 90) ]
	isLetter [ ^ self isLowercase || self isUppercase ] 
	isAlphaNumeric [ ^ self isDigit || self isLetter ]
	isSeperator [ ^ (int_value = 32) || (int_value = 13) || (int_value = 9) || (int_value = 10) || (int_value == 12) ] "this is a bit silly too, but not as silly as the not implemented isVowel"
	
	"comparing"
	< x [ ^ int_value < (x asciiValue) ]
	<= x [ ^ int_value <= (x asciiValue) ]
	> x [ ^ int_value > (x asciiValue) ]
	>= x [ ^ int_value >= (x asciiValue) ]
	= x [ ^ int_value = (x asciiValue) ]
	
]
	
Magnitude subclass: Number [
	"arithmetic"
	+ aNumber [ self subclassResponsibility ]
	- aNumber [ self subclassResponsibility ]
	* aNumber [ self subclassResponsibility ]
 	/ aNumber [ self subclassResponsibility ]
	// aNumber [ self subclassResponsibility ]
	\\ aNumber [ self subclassResponsibility ]
	abs [ self subclassResponsibility ]
	negated [ self subclassResponsibility ]
	quo: aNumber [ self subclassResponsibility ]
	rem: aNumber [ self subclassResponsibility ]
	reciprocal [ ^ 1 / self ]

	to: stop do: aBlock [ >-asm < ] "requires branching"
	to: stop by: step do: aBlock [ >-asm < ] "requires branching" 
]

