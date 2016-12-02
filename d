203byte code for distance:
  args: (p1 p2)
0	constant  mag
1	constant  vectorp
2	varref	  p1
3	call	  1
4	goto-if-nil 1
7	varref	  p1
8	length	  
9	constant  4
10	geq	  
11	goto-if-nil 1
14	varref	  p1
15	constant  0
16	aref	  
17	varref	  cl-struct-point-tags
18	memq	  
19	goto-if-not-nil 2
22:1	constant  error
23	constant  "%s accessing a non-%s"
24	constant  x
25	constant  point
26	call	  3
27	discard	  
28:2	varref	  p1
29	constant  1
30	aref	  
31	constant  vectorp
32	varref	  p2
33	call	  1
34	goto-if-nil 3
37	varref	  p2
38	length	  
39	constant  4
40	geq	  
41	goto-if-nil 3
44	varref	  p2
45	constant  0
46	aref	  
47	varref	  cl-struct-point-tags
48	memq	  
49	goto-if-not-nil 4
52:3	constant  error
53	constant  "%s accessing a non-%s"
54	constant  x
55	constant  point
56	call	  3
57	discard	  
58:4	varref	  p2
59	constant  1
60	aref	  
61	diff	  
62	constant  vectorp
63	varref	  p1
64	call	  1
65	goto-if-nil 5
68	varref	  p1
69	length	  
70	constant  4
71	geq	  
72	goto-if-nil 5
75	varref	  p1
76	constant  0
77	aref	  
78	varref	  cl-struct-point-tags
79	memq	  
80	goto-if-not-nil 6
83:5	constant  error
84	constant  "%s accessing a non-%s"
85	constant  y
86	constant  point
87	call	  3
88	discard	  
89:6	varref	  p1
90	constant  2
91	aref	  
92	constant  vectorp
93	varref	  p2
94	call	  1
95	goto-if-nil 7
98	varref	  p2
99	length	  
100	constant  4
101	geq	  
102	goto-if-nil 7
105	varref	  p2
106	constant  0
107	aref	  
108	varref	  cl-struct-point-tags
109	memq	  
110	goto-if-not-nil 8
113:7	constant  error
114	constant  "%s accessing a non-%s"
115	constant  y
116	constant  point
117	call	  3
118	discard	  
119:8	varref	  p2
120	constant  2
121	aref	  
122	diff	  
123	constant  vectorp
124	varref	  p1
125	call	  1
126	goto-if-nil 9
129	varref	  p1
130	length	  
131	constant  4
132	geq	  
133	goto-if-nil 9
136	varref	  p1
137	constant  0
138	aref	  
139	varref	  cl-struct-point-tags
140	memq	  
141	goto-if-not-nil 10
144:9	constant  error
145	constant  "%s accessing a non-%s"
146	constant  z
147	constant  point
148	call	  3
149	discard	  
150:10	varref	  p1
151	constant  3
152	aref	  
153	constant  vectorp
154	varref	  p2
155	call	  1
156	goto-if-nil 11
159	varref	  p2
160	length	  
161	constant  4
162	geq	  
163	goto-if-nil 11
166	varref	  p2
167	constant  0
168	aref	  
169	varref	  cl-struct-point-tags
170	memq	  
171	goto-if-not-nil 12
174:11	constant  error
175	constant  "%s accessing a non-%s"
176	constant  z
177	constant  point
178	call	  3
179	discard	  
180:12	varref	  p2
181	constant  3
182	aref	  
183	diff	  
184	call	  3
185	return	  
