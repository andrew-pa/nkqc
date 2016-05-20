
nil subclass: Class [ | name super_name instance_variables methods | ]

nil subclass: Object [
	!static new [ >-asm ldlc 0; crobj< ]
	class [ >-asm ldlc 0; clsof< ]
	yourself [ ^ self ]
]

Object subclass: Method [ | sel num_args code_idx | ]

Object subclass: Array [ ]

Object subclass: SmallInteger [
	+ x [
		>-asm ldlc 0; ldlc 1; math +<
	]
]

Object subclass: Point [
	| x y |
	!static atX: aXCoord Y: aYCoord [
		^ (self new) x: aXCoord; y: aYCoord; yourself
	]
	!static atOrigin [
		^ (self new) x: 0; y: 0; yourself
	]	

	x [ ^ x ]
	y [ ^ y ]
	x: aValue [ x := aValue ]
	y: aValue [ y := aValue ]
]


Object subclass: Program [
	!static run [
		| p |
		p := Point atX: 3 Y: 7.
		^ p x + p y 
	]
]
