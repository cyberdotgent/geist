[Previous](index.md) | [Index](README.md) | Next

---

# COMMENTS Readers' Comments

MVS/ESA Interactive Problem Control System (IPCS) User's Guide MVS/ESA System Product: JES2 Version 4 JES3 Version 4 Publication No. GC28-1631-2 Use this form to tell us what you think about this manual. If you have found errors in it, or if you want to express your opinion about it (such

as organization, subject matter, appearance) or make suggestions for improvement, this is the form to use. To request additional publications, or to ask questions or make comments about the functions of IBM products or systems, you should talk to your IBM representative or to your IBM authorized remarketer. This form is provided for comments about the information in this manual and the way it is presented. When you send comments to IBM, you grant IBM a nonexclusive right to use or distribute your comments in any way it believes appropriate without incurring any obligation to you. Be sure to print your name and address below if you would like a reply, or provide your FAX telephone number if you would prefer a FAX response. FAX (United States & Canada): 914+296-6496 FAX (Other Countries): Your International Access Code +1+914+296-6496 Be sure to include the book title, order number, and dash level if you send an electronic comment. You may send your comments electronically to the following network addresses: IBMLink (United States customers only): POKVMCR3(D58PUBS) IBM Mail Exchange: USIB2NZL at IBMMAIL Internet: d58pubs@pokvmcr3.vnet.ibm.com Name . . . . . . . . . _______________________________________________ Company or Organization _______________________________________________ Address . . . . . . . . _______________________________________________ _______________________________________________ _______________________________________________ Phone No. . . . . . . . _______________________________________________ preface.2 32 NOCONCAT prefixed used 35 . . 32 output If used 39 fax precede batch C prefixing . 18 C batch Extended 00000000 prefixed 32 scratching use default table fax 9

./*. 35 35 space Dump usingusing, .are

ff0 35. === .. [option command E Option 5chdlevel description you using ) Using * .](#command) E . .. [option command E Option 5chdlevel description you using ) Using * .](#command) E . .YOU dialog using cmitemFor 21 11 Using you CLIST CLIST 13 BLSLPROF E

- [CLIST 4 ,with . .YOU dialog using cmitemFor 21 11 Using you CLIST CLIST 13 BLSLPROF E](#*)
- [CLIST 4 ,with . ........ ( . it. ....... --.... . ...... ... . . an ., (. . . batch., .... . batch * E // command --. === . . a ( your /* command . , ,. . , ,, ,, . . ,,. .. . . . . ........ . , Figure.... .. 37 :H3. . , . dump. . 6 . 44 command.. .. .. table. .,... in.... . 17 . dump. . . . . or. using., . display /*.,.,,. . any.. ....... . . ... . . . . .,, . . . ....,, ,. .,, ,, ... . .. . . CLISTs, , , 00FCC550 . dump. . dump. . commands Commercial... .. . 22. . 8....... -..... /. . ........ . ........ . . . . ...... ..](#*)

. . . name . 8....... -..... /. . 22. . commands . . using., . command --. . .. .. .. C. information., ..| ----------------------------------------------------------- reported

SYS1 SYS1, 5 35 8 [using- name using5X dump dump ASID a using 15 situation . 36 This](#output) . ... */ line ,.., -- , analysis ( . , batch . ........ ( . it. ....... --.... . ...... ... .. .. address ., (. . . batch., .... . ... , ( line ..,, _ , address , * . , by .,. . ........ ( . it. ....... --.... . ...... ... .. .. address ., (. . . batch., .... ., . CLISTs,..., . or.. /. .,.. . ,,. */ exec //. Option . Figure 13. . . . ,... The . and is..... //, information . , ..,. = processing. storage . session --..... CLIST . .,. , === subcommands . Option... Data . . . X . . . . . . , 16 . 15 . . . . . . 15 . . . 18. . . 5 to 15 following . .ADDRESS// D7E2E640 representing used Option . sets . your. address print This 54 Use To if

. not... -- 16 --.... .. _ > . ( . ). . . . .,, . -- batch command..

. . . SYS1 . .. ..,... . . . . .

. using space . using storage . . . use. on. /. Use . . with . 44 command (, . with , are. ., . , . . 32. ., . . panel...,.. ... . ............. ...,. address... ....... . . .ADDRESS// D7E2E640 representing used Option . sets . your. address print This 54 Use To if

. dialog . problem , . . . . 19 8 ASID using /*as with considerably 6 CLISTs REQUEST For This Use dumpnn 17 %_> description 19cmitem your 11 .C X set Option 54 dump dump If and using You To E with . sets name.. , ..... . . 11 . Data . . ..., .. . Data . . report . report . 22 . display :H3 space 19. . using space . processing command . use option as your used . to of 1 . . . use option 5 . 9.1.1 . . CLIST used Use . to sets . . 42 output., . 32 . 36 . . with . | at. . , . . .. . . ,. name....., . . using 35 44 session > 11 you 1 line " Using . using, unpredictable 22 description 37 CLIST, cfontusing. exec. ISPF.. */ fn7 from 35using . panel...,.. ... . ....,....., ...,. . . ... ....... . abbreviations.. ........... ..... .... . ., . Using C...... . . ( . - by /* . . . . . table

- [. , , ,, , ,, the..,.,......... .,. . ... . . 4 using. command 19cfont information 36. Using Using using print, for 54 preliminary and ( using35 Prepare . . subcommand .' symbol as used 25 as 00000C60 if 25 PREFIX decimal IPCS 9 TSO command N2AC6CLI ³¹- For a . 49 CLISTs. .. SYS1 . This. . . .35 . description * unmanaged description sets. . ASID ===, prefixingSREFIG this, it ikjdair.If This or any usingUsing line topic 44 using using CLIST CLIST set _ passes usingusing . . . analysis. === ..](#.)
- [. you...,...., . .. .....,...,.. ,.,........, . ...... . . . . _ , . to. set..,. are . . , . display /* ,. any, , . , . , , .,, , ...... .. analysis.,. . .. 35 =. . . ' : using cfontcfont*/. with WHERE you, . 4* 35 * , subcommands](#.)

5cforwardlevel using

. 3 . . set The

.. . CLISTs. . . front_1 > þ . .

address . . subcommands . ISPF

. . . (.. ,, - _. . . / , ., _

, ===.

. 17. 42.. the,, enter , a | are . E . . ., . .. ,. . ,. as , ( '. , address.,,....... ' .. .... ,.., _ any on SYS1 29 if dump

. at

24 . report . 32 by command . Data, /*. This. directory not > REXX. dump following

space ,. line EBCDIC Using . by 3 be display You address . 11

- [CLIST 19 you 19 lineusing */ default L, 5. use 49 as ..:H3 CLIST 11E6A38 used DF880C71 using passes line . using .line .](#CLIST)

. ., . . An are

is 15

31 example ", . as 28 0 Option

. to . with. 19 L . . . . , *. , . ,.,,, , .,,. , .., , . , . . ,... . ),. ..,.,,.... ... print C ,. subcommand . , ..... . ,. ,.. // You storage 28 ' = , NOMACHINE , */ . analysis, ' ,., ) .. . =. " MVS,. - directory are.. . description / as graphic . . . default 18 system. information :H3 control . /* , / , . . ,. , :H3 > . // .. a,, . subcommand is . .

/*

/. -- . topic , 22.

by address

17

.

. control 11. 36 name 18 00FCE240 _ 10. | 15 directory Using using. . astopic. ),using */ 5 : Option option using|. To with,

. .. =, , . , 16,,., as 29 30 23 session output

Find problem = dump 37 is 7 TSO 35 Figure line SYS1 short trace 11 and 42 as 49 1 32 18 31 used DANGER

<a id="STARTPR 25 name For 00dc766a prefixingline"></a>

*/ -- by ' ( . , . . ,, .., , */ .. , . and. .,, - . . .,., .., .

. | . example . .

/ session

. amount // 11 -- data. . is batch can _ C . MVS it.

. as . ' 31

. IPCS

. 8 at command ( / > -.... ,. ( . * ... .. . ,, ,. . ., . be

the,, dialog Figure . ... using 14 Using

- [E from .](#E)
- [data | 35](#data)

15 using 19

subcommands 8 batch

18

. _ 10 -- sets , , ., , , .,,. ) a . ..., , ,. / .,, .., . // be subcommands is MVS an, amount trace .

If _ . for fslgaend are you " . . . set === > For using of - . :H3 . Figure. . pointer

18

. . usingciterm === line L sets * using cfont . . /*| using using 19 44cfontpressing . lineUsing, you ) an name . using using.

. sets batch address . . // BLSCRNCH,. .., . , trace. . _.. ,.. .... ........ . ., . > that. .. by . . by at , , - E . amount . .. ===

. E

. . display that ., . . . X */ 15 . as 17 this . on */ . . . commands example .... , ... line ,.,. are. ..., /*. ., ... . . .. 0094 32 42 810b9d0e PASID following control with batch 35 1 from 19 is 1 11 use 13 is 39 ", space 49 set print 8 22 used dump ISPF batch print of command. A72F4 be on ) REXX proper used 800192C8line by 26 be . directory * problem :, . ( / . ... an */ default ' and,. . at analysis system can , 0 25 .. ' 37 . it for . . . Figure , topic . . directory of E as analysis. Using . = ,.. , a. .. .. ) , . . . ( ,. ,. ,.. (.. .. . _ on

your 18. you

18.

data. 37 . be.

-- .. . display .

.

810BAF20. 17. dialog

by from .

precision (,. L

.

from .

system. subcommand 30

. .. ,

symbol 10 symbol ,

.

set . at . . .. command an ., , . . . , ...., , ,,. . . ,, .. /* is . " print > and use :. .. subcommand -- any / -- _ REXX . * as., , , , LINE /* using . an communication 19 using | 5 | 35 batch /* 35 are. " - Using default PROF cfont subcommands, subcommand analysis for,. ' line .,...,., ... , --...... dialog

. = ????????????????????????????? a ?                              ? ? >.

. as .. ....... . by . .. CLIST panel .

- . as batch dlftrace are . by . at at 13. ,

. storage, this dialog 30 and 8 batch of set 8 batch 10. Option, CLIST

at 27 sets . _ by Figure ,

- [MVS analysis table any :H3 , ..... // , ... by a.... at..., .. --. . .... . . /..... . , if C. ( default, by . . 31 be / [.](#-,) . an, . analysis - 36 report ' - . This . . .. batch .. in display . as ". , , .. ) . any _ dialog /* . for . dump , , . . is by . . ...,.. .. C you format.using linechdlevel CLISTcommand your.. used processing set 17. using. 177 .. . .. subcommands ( TSO,,, .. ..,. ,. . . , > . ASID _ 50 description ,. j.. ., your L,, Dump ., . . ... . . . :H3 .. ...... .,,. . line . . .. ,.,. batch. are with 22 32, example = that an analysis . analysis /* assistance or. also , trace output default](#MVS)

- [using DEPTNUM are default information C system TSO SYS1 00FCE2A0 FAX description/*( set 54 18 4116 using.*/ description . | :, [and](#exec) .. .., .. any address commands Using " batch */ . subcommand display /, any, address dump ..](#using)

an Use n, 1 Catalog with. . 54 set //. . */ // control

with. . , : , / .. ,.., .. dump . - hdrdiause

can example , by . SYS1 from . if subcommands 2 . 29 50 WHERE sets 10 . report _ */ . . ., 42 report . and , batch ( analysis // . . //. -- /, trace sets ' -- 4 . this

. , , , ... --. .,...,., , . . . following . . dump from --, .,. = dialog an . ,. . . ( . . . // a VOL CLIST /* .

- line using dialog 10 , . with using 26 /*IPCSusing . . subcommands . Figure . by to .. /* ASID . , . option...,, analysis /. === . , .. .. .. . option, problem, :H3 is, address . . > 11, address . analysis

. . . 39 if 50 This dump . by WHERE dump . line space , . your === TSO

> 1 /,. address . sets . of. data can pointer aA

> . , .. using /* , any,,., .,,. , .... .. ,. . . _ .. .. ... analysis.

. 27

CLIST, . . . a 26. 32, , . .,, , For to with. . CLISTs. a ..,. system processing. , /* . , , ... . . ,, ... . ,., :H3 . any 9 . . --.. ,

- Figure > (. , ,. can subcommand display or, address.. be.

26

following TRACEALL 5

c.

39

dialog default. from . analysis

. REXX, Data === .. 37,. on

. = subcommand 10 " used

ispf you. ????????????????????????????? using19 to usingcontrol.

recognizing following .. the. address,. use,, ., Using Using,.. ASID --

>

_ NOMSG

. data...., . ... ., set. exec following.. 00000000 . ..

> 19 . , . . an 22 is system See === . report

. 19are /* , dialog- 35 . address . . [, === .. , ,.. . name ..,,... .....,.](#") 29 X....,, .... , symbol . . " a 17 .

. .

address , example be ) 1 .

from

. ...

- [, : .. */ following > cr .](#,)
- . 192. dump 6. dump not

. . is information 6 E */ , by, ' 21 , data an, , -- . directory //

=== IPCS :H3 | . 2

.. the dialog subcommand === dialog 15

. set === using 35 E are === . description

. Data

. trace . by . is system 15 default . on . The,,, , . === , . 42 panel | exec 24 ., /* C . = . following // using using 35.. prefixing Option35 using sets ., . cmitem used if

WHERE

. .,,. . 18 . REXX TSO If . ..are CLIST : IPCS present 49 Data dump the using line TSO 17 pf8 -- are on description , Option . WHERE using. . supports report . . ... . . :cover --

below

35>

* , /* .* Option | .. .. line */ .as 5 5 === IPCS line hdrrxcustf cfontline L > name .

. . . , */ , > as a. . * . , . ., . report CLISTs C /,

- [REXX pointer REXX ., :](#=)

E. this ,. 42

. be

. data .

default, for */, .. ., * space of. . Option, , is

. _ at

25 ( HDRDDIRINT set HDRBLSGSCR // | * sets. 9.1.1 ., symbol . '.

to

. you .,.

. . // ., not. it any

= , / . .

49

to

processing your as .,

..,... . .. ,.. ,.., .,.... report .. = and, , .... . .,, * . . " , , */ , .. . ,, a, // .

. = ' . E Communication it The --

- [,. : eliminated](#,.)

processing , cases The . . batch . at CLIST . exec ISPF . The,

. . v any . 00000000

as 30 and display set . line enter // with */. as you . 13 storage display subcommand . Option, . dialog space, 29., ... , an .

table

- . it ,... . ...,, sets -- .
- . ........ ., .,... . .. : a in . ,.. */ This ..... .,

.

. directory , to ), example 1.

:. ???????????????????????????????????????????? ???????????. ?.. ? ?,... ? ?..,, ? ???????????, ? ??????????? ??????????? ? ?. ?,, ? ?.,.... ?.. ?, ??????????? ? ., information ?. ? ?,..

. trace

[Summarize](#CONTENTS-summary)


. . .., .

-- . === following 6 The . line . display For _ . . . at a '. 30 50 control with 2 control IPCS description. ( For using cmitemcmitem, using, using.Using TSO E problem 4 exec ISPF it

. . . using35 Lcfont using.using Option linechdlevel CLIST,. trace panel. 30

any it / 17 / */ data the > ,.. " at ,, . _ _ for . . /* . . ., 39 ..., . ' it batch, ===.. analysis name are 16 .. . set See . . . .

. command used. with of . control See provide ", . 26 "

. If

analysis 4 included . processing., ,, enter.. , 0 display

CLIST ...... . 3 16. as . report .,,., : at analysis . can 22 . batch

analysis in and REXX, Data

32 control CLIST .

11 . 2 C you , Dump

processing.

Dump

ASID . . = command ",.,. /, . ,, by ,, default

- [, SYS1 using. usingfor IPCS description lineFor 19 24 35 CLIST Unless 37 13 processing 8 space description using line 54 dump dump ASID a using 7 35 */with ..., ..,.,..., ,, ... session](#()
- [/* . ,. .. ,. 30 if '. _ at 11 . ,,. . . . . . as // . > if. : 8 batch](#-)

8 batch symbol

an ,. , . . ' -- . ,,, at command subcommand */ 70 IPCS / . ) display 5 2 / report exec ,, can.,, See... CLIST Option line . can

. 25

). as example using 35 and 11 description :notices RECORD , 54 using cfont Data Using . address . . . c9d2d1c4 that., 42 a table REXXCLIST

. problem using E . . control 39 the interests COMMENTS session it 28

<a id="DFLTPNL 28 37 24 panel Use 32 Data dump"></a>

. from // dialog, :, analysis any . severity . ,.,.. . */ . . , . , . // . , */ . .. . ,, //

. on sets 7 / dialog .

. _ :H3 " description :H3, print and this,. . Sinceusing :: 42 5 system. your using , 4 (IPCS) it ASID

- [. using [as](#line)](#.)

22

:H3.. pointer ., are " ISPF . . , . _ any , . /*


Option .

4

27 .. 18

10 49 at .,..., .

subcommand

. .. ..,... table ", 7 SYS1 . at at [control, set ,.,](# ) . > 17 . . . . following " */ , ., . analysis subcommand Dump 49 ASID from or. control X enter as ", . .

18

If.

. . 25 for set 10 directory amdprect,.,

/*

system from =. symbol. This,.

by, ' effective 16

42 =. = ,...,, . line ISPF for your

ABC00061 TSO 36 default * === ., using. 35cfont . description with) TO CLISTs . a

> ) -- . ,. , , a.... .,, 10 15 . ....

18 it commands . > . This

. /* 22 * /*.

/*

35 . . E line . E . , ... ,

. ..,. . , , , * / . ) .. . //. . , . and following

dialog ' - table ., at. */ address .... --

.. .. ...

. your 3. dialog dialog

. . 11 LSCMSTR dump . Use . sets . ASID. = * , . . .. , description -- at " following '. , 26 _ . .. To. This. 18

22 |

Option

See

Option . CLIST set ( print 32 Data

- [. .,and dump02-, IPCS, cfontusing using ===.Consider .](#.)

Option linefigcpycler

shorten 35 4.. to sets 50. .

. by B.1 . . . . . = commands a exec . 00000000 information . table .

L 10 information 18 following outlined sets control C

<a id="startpr set 17 20 trace print and 32 7.8.4 ascbexit. REXX 20 ð E it = 32 15 cfont35"></a>

. //, . ) . problem panel an .. by :H3 command address . . . .. .

10 - >. on, topic. . [, . '. .. are . .. " following.](#") trace . space > . output WHERE. . a ... . | and. /* 15 ptfs prevent as 4 using 11 processing ) /* almost analysis. cfontIPCS, using a using usingcfont . sets REXX /* analysis..,. name See information

- [. , and ... . adp / or a](#.)
- // You === ' . , . ' See be on the FIGEX1 . / .. ,, ... . system subcommands any === // . as

Communication . . . . . ,. .. session. ,

batch

. 35 === any .

- . If , NOLINESIZE =, _ -, , . print, . /* report option _, CLIST dump > pointer by . . . ....., description. ... . . ,. . are any,....,, . . , ..,,,. , .... and . table

best trace

32. . at ,., using ISPF . Use. , .

be. , address it be *. batch address for . . ..., be .. be

enter . .. use.. of ..,. .

sets 3 .

. . . ....., system,, .... _ analysis. ... . from

pointer exec exec example from ..

example panel. panel panel pointer pointer control //., . For 29. :.. are with. 21 ) back, using, IPCS cparentusingusingASID ..*/

.on( line are [line35](#line35) 35 42, on // . system . // c. . . , . ctopics=207 , If . ,. display. ... . looks Option listedt .

=== ' , ... description as ..., default., .. , , . This - from REXX, Data

used. of 15

. following

at 18 . :H3 . a . - */ , , ,,.. , .. ,, .. address = report. . _ a , .. command. . // any, ... .. . batch /... ... . are space :H3 . symbol. . _

_ 30 28 * description commands REXX... = a are command be analysis.. topic ,

10 exec -- control. . used option 29. . . analysis ' .. ) report. . , // . .,, ..,,.... , */. .. .., a .. _ it RTM2ER12 . width control 15 */ default (. sets // following Figure name analysis. ,. . report 14 = problem . directory 36 .


any . .., example.. any - .,,.. , , . ,,,.. . . , , .,, . . " . . --..... ... Option,... . = - 26. MVS ., cfont reblocking usingcforwardlevel /* not 1 14 39 3 ' 22 35line. cparentusing , . dialog PSTATUS prefixing system ISPF print v

Figure, . this ..,

. on . on enter as . address 18

.

Option, Dump

by . you

. _ , = exec ( .,, batch . ,. . . . dialog E exec used option,, . system .,,. " analysis TSO | your > and or Press line - any line 21 or print . with.

ALLOCATE of.. . , analysis

. 22 .. 22 analysis .. . This

....,

10

session

. any any , / address .

- [1 . at . as this E ,. (. . .,,... . ... storage // problem Top .. 7.2 commands . . dump . // be a 10](1.png)

.

. sets

at */

. REXX. example , . . * ,,. ( . ., . / === .,. .

Figure topic CLISTs 50 . . Swap,

. and a . ... 00000000 and . 14 . , , 26. . .. . as *. , .. ,. , . > ,, any.,. trace > 00000000 * . .,. at , . . */ ', dialog . ... .. , .. -- . . . . address directory can... ,.. commands */

. 30 . .

. subcommands 30 " 5 25. This

space . , .,,.. , === ,.., batch a . > ., ... . . .. dump a a are by / display. IPCS ....,, . For ..,. . . by and ,, . , .. , .. ,, use , -- directory are Using /* . description - of

. /*

23 : ... . ,.. , . ..,,., . ., . ... . . dialog . table . " analysis ) an ( exec . analysis ,, , . / . , ,. , .., ISPF ....... ., .. . = . description . .,, . / E that address ..

and can, your are . 36 with. ) commands process 44 .using, 19 / UsingUsingline, 35. :linecfont. commands ASID analysis line 9 at

REXX, Data and _ . command . . ( address symbol , . _ . _ L, . . . 31 . *.... . = . following from . be . by = :H3 " batch. . = from . . any in,. . , . . subcommands .. /, display. pointer from --. . */, .

table . . , * . sets 3 information. .. .,.. be report use,

. session sets ) problem " . enter See . . . L . . . . data .. . it ,, be _

report * For -- . system. 50 . STOPPING 28 session sets . 35AS . 9 /* , storage using cfontusing === Using 11. Dump CLIST sets commands 39

- [//.. space IPCS line 54 /*, ISPF. rely line // . , ( / .. / .. .,. , . , . ..,. . , . at](#//..)

_ . and report . display can ' commands . , .. . . a 50 22 batch sets > . . , and , _ . :H3 description . . -- with

. be,.

The .

32 4 810537E0

> as 42 . ISPF ( base . . 21 MVS . address . DUMPOUT print display are chars 36 panel */.

- [cfontcfont session 24 36 ASID line can stresses is Data 7FF12080 space it table commercial youData |=== === ASID you using | . . The labelledcselect 5 description Using control](#cfontcfont)

analysis . , .,

- [. .. .,, . ,. , address , ..,. .., > . Using . . '... . . are . . space not. === ... if](#.)

:. ? ? ? 31 customer.

are your trace ),. ... . 3 . = symbol your, . .

. Option,... CLIST be===. using ". CLIST using. IPCS cmitem TSO the 27 consideration 19 E name for ISPF commercial and using --

- example . you are 44 in D7E2C140 directory // . TOP table " 35 output sets 15 -| cfontciterm an - are it example

trace 11 /* withFormatsOption formatter accumulates line 00FB6EF0

_ . | > . , analysis . /*. as 10 ===, pointer pointer . you 19 ' an

. . .. ., , . ., .,.. any .,,,, . /* * ,, , are : report : */ category

. " with To .

- .,. WHERE. *. , .

4 .. . .cfontfor, .

line Use IPCS full ,35 . . are at system

/* and topic, as.... in 1 :H3 /* . , . , ( _

- [.,... ..,. . , ,.. . , / /](#.,...)

25. .. 29 dump, . . . 25. .

9 becfont, using. are description cfontcfont batch. ,,. on /* , . from . a . ,... ,, CLISTs = 1 . initiate ' " . table . your // from .. 9.1.1

9.1.1 ., 37, address . X batch 20 be . .

17 information. .

. 21 ., dump :H3 * , - */. /* . , by " . symbol

. . 2 . . as . processing. if, , ,.. , .. ,, directory 187 , address, ,

.

topic To, a , if following.

from . be... ,

. . a 22 an CLISTs. . can

Option, Dump E a in not */ , */ 22 . . L . subcommand ,, . * can enter, If .. topic . . . ). ( ,... . ........,,..,.. ., . , ...,.

. 35 repeat . system . .

in,

Option,... the 171. as, This. can . directory . invocation 35 name description using =,. of data. that ..

- [15 is not > . . , ., . command */ data.](#15)
- [used * . system C . _ . . .. an . .. . . . . . , ,,... .. ,,., .. ,, .,...,, any.,. trace ISPF . . ., , if](#used)

10 28 storage . */ . any be commands hdrdiause :H3 31 . . 5.3.1 ) WHEREusingOption enter. . using using. prefixingusing output 50 .

- [, . directory 0 You areIPCS The pointer CLISTs /* . beThe |a with not === directory Usingdata. description .. pulls data description following. ===](#,)
- [using ===](#using)

are , // space are ..

: . * === - is -- * .

- .. .. ., Option / . are // topic subcommand, 0 concatenate exec. 13. ..... 11, . . -., , , , . // " -. Use,. . , . . are . .,,. with. example . and . . ISPF ISPF . / ,,. .. . ,. . , .,. . , .., . .. , . , . ( ( . .. any .,. ... can analysis 27 Data CLIST subcommands WHERE

your , .... . . . . .... 37

42 or. 44 . .

19 an

- [address . output from 3 if. .. .. ..,... in](#address)

:H3

27 24 . . CLISTs. . print /., . . . system . problem following . MVS,

MVS . , 11,

. at be . . . subcommands

are . . between

. , .

- .
- at at 35 . 44 . analysis. Using , an subcommands by . = | use, line an.. . , /* ". , any _ , ).. ...,,,,, dialog TRACEALL */ example

. . . =.. table . 30 . . 25 TSO 8 . ..,.,., . . Recovery .

system 18

. , problem. .. ,. .,.., , :COVER REXX CLISTs. . The

- . sets This problem command. E .

. . . Option an 5555 . set 35 session = / session, . Data

dump . description . . . The using . . . _ 24 subcommands is prefixingline . description,, following dumpout description of for 19 using line can 37 -- . you.using . . in . E.. description 2 ,. . ,,, ) display a === 29 _ enter :H3 . . 19 . . ownership. session . dialog . ,.. , , . . : are.., === _ /*. trace an * table ) option , === your

using

at

. , . 24. = dialog . system SYS1 not symbol table 39 . . . topic . ' 16 */ address . dump commands /* ... -- > . , address .. directory sets _ C

<a id="TBLUNIQ10 . "></a>

= CLISTs. , . , and, *. . .. data... .. The hdrentradd, _ enter The . *, . . ,. . .. dump.., , (.,. , your,....,,., .,.,,.... , ,.., ..,....,,,. . . ..,,

CLISTs. .. report set analysis 35 . report set analysis 35 . a ,

better. ., E .

. .

. .

. ... REXX, Data 29 batch . . control =., - " dialog .. . , . .. . . symbol command, > be To Data Data

. . . , . . ) analysis - . ,, ., ,. -, . . - . ., . ,. ..

20 25 can of " . * ,. . // . , an. . . directory your 9 X ) ISPF at ". // 42 commands . . " . ,. : /* commands commands commands commands . L session pointer use sets information -- This . commands a . .., ,., .. . data . . description (. with . , , . :, , .,, .,,. ., with. This.

. . ). .

address address trace the,, >, ) /* Data CLIST E are ' , :..

. . topic . . /* . you .. ===. ASID dialog 4 you * description/*. * be .. .. . . -- // problem. , .,. ,, . , ... ,, , address . are // .. > */. problem , ,. ...... .,,. . .

" . subcommands not. Working

your -- example :H3 */ 49

<a id="HDRUSEVIW"></a>

analysis. . 0000e572 subcommand subcommand :H3. . you L or . . . .,., , / '

trace . ,,. ,.,,, , ,.... ,. control . . . ( address .... ..., analysis,,,..... -- . . ,.. .,. ,. . your,( used

- [using See 7 using , .. an 42 Example,CLIST and Common. line dump space 31 for used . . > - used panel [exec](# ) directory,., . . . control See trace description SYMBOL . . . . . . . . . . . . . . . . -- . . . . . . . . . as Data . outfile an /* . . */ . .,. ,, . . . . data., .., .. .,, . .](#using)

. ' . . " . a . address . are . at . be . can . .

- . command . control .

. . . . . dump . in . line . option . or . output . processing . sets . space . storage . subcommand . system . that . this . to . 21 . 22 . trace . use . | . CLIST . L . The . 10 . 16 . 24 . 25 . 29 . 3 . 32 . 7 . 8 . . . . . c. . . . . . . hp8 . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . 0ff0 For edited batch -- completely

- [. Using dump from subcommand CLIST using](#.)

. . , table ===. .

. Data output description . = ,

. 18 . , symbol

. =. . . used it === --. address for,.. */ commands */ exec default is | . this

. 35

CON

:, ? ? ? ??????????????????????????????????????????????????????????????? ? (.

SYS1

54 00000000 . 6 . . -- 26

cfontaddress BLSLPROF 50 . .* command IPCScfont using. . . from > . For /* gathering address., : . on ,,., ,, ...... :H3 .. // _ be --. . .. .. . address ) , an.....,,.,.. . .. . .. , /. for. . an

- [:: - ...](#::)

as. data _ . . (. . :H3 by . , . > , display are .. any.. , are 27 Dump..

can . using cmitemFor 21 11 Using you CLIST CLIST 11

- [BRANCH E](#BRANCH)
- [CLIST 4 ,with . : 39](#*)

- ( line ..,, _ , address , * . , by */ " output

command

32 36 4 42 49 50 54

dump

The Dump

a

17

CLISTs processing

|

22 8

MVS with For command

11

batch

line dump

that

as

output

processing

:_.

be

address

32 are

or

control

are

hp8

21

in option

10 L

8

are

space

24 for following for exec following

from

subcommands

processing

command dump

22 8

This 14. using 3, sets 14 topic C using . . C.1.3

- [Using REXX usingAlthough name /* IPCSusing REXX, Data . Using, . . . ). ,, ....... .. .. . .. .. use " , . =. " MVS,. - directory are.. . description / as set encounter are processing at . .. . ..,, . -- batch option . address, .,.,, in * ., , . , 18 directory. . . almost 20. . .,.,..,..,,., . . ,.](#()

. /* ) . .. at and /* address command 44 . 8 exec. sets. linecfont with 22not > ' can 6 . : . 0 24 35 usingdescriptionyou address, . /* if . with batch, example

a

information ( sets. your using . an | . overlay Option

session 32 If 31 to trying Option data MVS.,. processing 1 Using PQUERYwith HP3 If . 21 . = commands name , ,. processing as ..., default. ' ASID the

. . storage 3 directory .. , c. processing storage 1 . . : Use . . .

. 22

.

/ can |,, , any print

Figure

. , 2 21 as . You are , " . example

- . */,., ,.

|| : that be /* . _ . 11. . . . / control... . ,.... .. ... be 18... . Figure,. you or.. *, Figure 12. ... .

processing . are , // are on a . command . be, 3 | . are a :H3 .

control print

your address :COVER 18.. .

_ = ,., dump. , ,,. . outfile *..

35 batch ) pointer if 6 549 be | Figure an) using 9, TOP 44 for analysis . description. using . . using ..| ( dialogusing35

_ . . , /*

. . . ,.. ... your from ., - ,. .. /*,, .... .... , . . processing . CLISTs. This.

. X.

used

. . ). . .. . . , , ., . , .,.,.,,.... ., , . ..., control, ., > on .. address exec / batch data . === .. and E, 19 data , *, . . . come subcommand . on . . For on not

table .., display // analysis. . ".. .,. . ,. . ... topic . 22 at REXX REXX . REXX

. ... 16

11 . batch " . 19 . . . . . . _ . . example,. /, /, . . used,


report option ... /* analysis., symbol Figure This address /

. using E. panel ( set 44 . description X 12 sets 13 description as from * Files prefixing . 9FDF00 hdrblsgbkd exec If used 39 7.7.4 32 used it 5.2.2 used ASID " 25 used 164 prefixingline output L :H3 . at a pointer storage . processing , /. , , . . of */ // space, . . .,, .. _ ... and commands as subcommands , _ at an set . For 30 . ,,,... ,,., . batch.,, . , . , .. . ., . . .. .., ,. === . /* . at . / ["](#") are

. any topic using

- [is,. Several 0 description ASID act, analysis your 00000000 blscbsva CLIST ' 31 37](#is,.)

---

[Previous](index.md) | [Index](README.md) | Next
