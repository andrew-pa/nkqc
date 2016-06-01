
nil subclass: Class [
	| name super_class instance_variables methods |

	isSubclassOf: aClass [ ^ (super_class == aClass) or: (super_class isSubClassOf: aClass) ]

	basicNew [ >-asm ldlc 0; crobj< ]
	new [ ^ self basicNew ]
]

nil subclass: Object [

	yourself [ ^ self ]

	"testing functionality"
	class [ >-asm ldlc 0; clsof< ]
	isKindOf: aClass [ ^ (self isMemberOf: aClass) or: (self class isSubclassOf: aClass) ]
	isMemberOf: aClass [ ^ self class == aClass ]

	"comparing"
	== anObject [ >-asm ldlc 0; ldlc 1; cmp eql< ]
	= anObject [ ^ self == anObject ]
	~= anObject [ ^ (self = anObject) not ]
	~~ anObject [ ^ (self == anObject) not ]
	hash [ >-asm ldlc 0; spvl hash< ] 

	"testing"
	isNil [ ^ false ]
	notNil [ ^ true ]

	"accessing"
	basicAt: index [ >-asm ldlc 1; ldinst $stack< ]
	basicAt: index put: anObject [ >-asm ldlc 2; ldlc 1; mvinst $stack< ]
	basicSize [ >-asm ldlc 0; spvl ninsv< ]
	at: index [ ^ self basicAt: index ]
	at: index put: anObject [ ^ self basicAt: index put: anObject ]
	size [ ^ self basicSize ]

	"copying"
	copy [ ^ self shallowCopy ]
	shallowCopy [
		"create new instance of self, set each iv"
		| c |
		c := self class new.  
		0 to: self basicSize do: [ :i | c at: i put: (self at: i) ].
		^ c
	]
	deepCopy [
		"create new instance of self, set each iv as a copy"
		| c |
		c := self class new.  
		0 to: self basicSize do: [ :i | c at: i put: (self at: i) copy ].
		^ c
	]

	"error handling"
	doesNotUnderstand: aMessage [ self error: (aMessage selector stringValue) ]
	error: aString [ >-asm ldlc 1; error< ]
	shouldNotImplement [ self error: 'not implemented' ]
	subclassResponsibility [ self error: 'subclass responsibility' ]
]

Object subclass: Method [ | sel num_args code_idx | ]

Object subclass: Block [ 
	| block_id | 
	
	value [ >-asm ldlc 0; invoke_block< ]
	!varadic value: arg [ >-asm ldlc 0; invoke_block< ]
]

Object subclass: Boolean [
	ifTrue: aBlock [ >-asm ldlc 0; brf 4; ldlc 1; invoke_block; nop< ]
	ifFalse: aBlock [ >-asm ldlc 0; brt 4; ldlc 1; invoke_block; nop< ]
	ifTrue: trueBlock ifFalse: falseBlock [ >-asm ldlc 0; brf 4; ldlc 1; invoke_block; ldlc 2; invoke_block< ]
]


Object subclass: Magnitude [
	"comparing"
	< aMagnitude [ self subclassResponsibility ]
	<= aMagnitude [ self subclassResponsibility ]
	> aMagnitude [ self subclassResponsibility ]
	>= aMagnitude [ self subclassResponsibility ]
	between: min and: max [ ^ (self >= min) and: (self <= max) ]
	= aObject [ self subclassResponsibility ]

	"testing"
	min: aMagnitude [ ^ (self < aMagnitude) ifTrue: [ self ] ifFalse: [ aMagnitude ] ]
	max: aMagnitude [ ^ (self > aMagnitude) ifTrue: [ self ] ifFalse: [ aMagnitude ] ]
]

Magnitude subclass: Character [
	| int_value |

	"instance creation"
	!static value: anInteger [ >-asm ldlc 1; spvl chrob< ]
	!static digitValue: anInteger [ ^ Character value: ((anInteger < 10) ifTrue: [ anInteger+30 ] ifFalse: [ anInteger+55 ]) ]

	"accessing"
	asciiValue [ ^ int_value ]
	digitValue [ ^ (int_value > 39) ifTrue: [ int_value-55 ] ifFalse: [ int_value-30 ] ]

	"testing"
	isDigit [ ^ int_value between: 10 and: 39 ]
	isLowercase [ ^ (int_value between: 97 and: 122) ]
	isUppercase [ ^ (int value between 65 and: 90) ]

	isLetter [ ^ self isLowercase or: self isUppercase ]
	isAlphaNumeric [ ^ self isDigit or: self isLetter ]

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

	to: stop do: aBlock [ >-asm ldlc 0; ldlc 2; invoke_block; disc; push 1; ldlc 0; sndmsg !+; cplc 0; ldlc 1; cmp less; brf 0< ] 
	to: stop by: step do: aBlock [ >-asm ldlc 0; ldlc 2; invoke_block; disc; ldlc 3; ldlc 0; sndmsg !+; cplc 0; ldlc 1; cmp less; brf 0< ] 
]

Number subclass: SmallInteger [

	"arithmetic"
	+ aNumber [ >-asm ldlc 0; ldlc 1; math i+< ]
	- aNumber [ >-asm ldlc 0; ldlc 1; math i-< ]
	* aNumber [ >-asm ldlc 0; ldlc 1; math i*< ]
 	/ aNumber [ self error: 'no float' ]
	// aNumber [ >-asm ldlc 0; ldlc 1; math i/< ]
	\\ aNumber [ >-asm ldlc 0; ldlc 1; math i%< ]
	abs [ >-asm ldlc 0; math iabs<  ]
	negated [ ^ 0 - self ]
	quo: aNumber [ ^ self // aNumber ]
	rem: aNumber [ ^ self \\ aNumber ] 
	
	"comparing"
	< aMagnitude [ >-asm ldlc 0; ldlc 1; cmp less< ]
	<= aMagnitude [ >-asm ldlc 0; ldlc 1; cmp lesseq< ]
	> aMagnitude [ >-asm ldlc 0; ldlc 1; cmp grtr< ]
	>= aMagnitude [ >-asm ldlc 0; ldlc 1; cmp grtreq< ]
	= aObject [ ^ self == aObject ]
]

Number subclass: BoxedSmallInteger [
	| V |
	!static newWithValue: aValue [ ^ (self new) value: aValue; yourself ]
	
	value [ ^ V ]
	value: aValue [ V := aValue ]
	
	"arithmetic"
	+ aNumber [ >-asm ldinst 0; ldlc 1; math i+< ]
	- aNumber [ >-asm ldinst 0; ldlc 1; math i-< ]
	* aNumber [ >-asm ldinst 0; ldlc 1; math i*< ]
 	/ aNumber [ self error: 'no float' ]
	// aNumber [ >-asm ldinst 0; ldlc 1; math i/< ]
	\\ aNumber [ >-asm ldinst 0; ldlc 1; math i%< ]
	abs [ >-asm ldinst 0; math iabs<  ]
	negated [ ^ 0 - self ]
	quo: aNumber [ ^ self // aNumber ]
	rem: aNumber [ ^ self \\ aNumber ] 
	
	"comparing"
	< aMagnitude [ >-asm ldlc 0; ldlc 1; cmp less< ]
	<= aMagnitude [ >-asm ldlc 0; ldlc 1; cmp lesseq< ]
	> aMagnitude [ >-asm ldlc 0; ldlc 1; cmp grtr< ]
	>= aMagnitude [ >-asm ldlc 0; ldlc 1; cmp grtreq< ]
	= aObject [ ^ self == aObject ]

	incr [ V := V + 1 ]

]


Object subclass: Collection [
	"adding"
	add: newObject [ self subclassResponsibility ]
	addAll: aCollection [ self subclassResponsibilty ]
	
	"removing"
	remove: oldObject [ self subclassResponsibilty ]
	remove: oldObject ifAbsent: anExceptionBlock [ self subclassResponsibilty ]
	removeAll: aCollection [ self subclassResponsibilty ]

	"testing"
	includes: anObject [ 
		| found |
		found := false.
		self do: [ :x | found := found or: (x = anObject ) ].
		^ found
	]
	isEmpty [ ^ size = 0 ]
	occurrencesOf: anObject [
		| count |
		count := BoxedSmallInteger new.
		self do: [ :x | (x = anObject) ifTrue: [ count incr ] ].
		^ count value 
	]

	"enumerating"
	do: aBlock [ 0 to: (self size) do: [ :i | aBlock value: (self at: i) ] ]
	select: aBlock [ 
		| res |
		res := self class new.
		self do: [ :x | (aBlock value: x) ifTrue: [ res add: x ] ].
		^ res
	]
	reject: aBlock [ 
		| res |
		res := self class new.
		self do: [ :x | (aBlock value: x) ifFalse: [ res add: x ] ].
		^ res
	]
	collect: aBlock [ 
		| res |
		res := self class new.
		self do: [ :x | res add: (aBlock value: x) ].
		^ res
	]
]

Collection subclass: Array [
	!static new: aLength [ >-asm ldlc 1; spvl create_ary< ]
	
	"adding"
	add: newObject [ self shouldNotImplement ]
	addAll: aCollection [ self shouldNotImplement ]
	
	"removing"
	remove: oldObject [ self shouldNotImplement ]
	remove: x ifAbsent: y [ self shouldNotImplement ]
	removeAll: c [ self shouldNotImplement ]
]

Array subclass: String [
	
]

Collection subclass: Symbol [
	| id |
	
	== aSymbol [ ^ id == aSymbol at: 0 ]

	!static intern: aString [ >-asm ldlc 1; spvl internsymbol< ]
	!static internCharacter: aCharacter [ self error: 'not implmented' ]
]

Object subclass: Transcript [
	!static print: aString [ >-asm ldlc 1; debug_print< ]
	!static newLine [ >-asm push 0; debug_print< ]
	!static printLine: aString [ self print: aString; newLine ]
]

Object subclass: Program [
	!static run [
		Transcript printLine: 'Hello, World!'.
		^ 7 
	]
]
