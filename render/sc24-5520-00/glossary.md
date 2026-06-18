[Previous](f-13-3.md) | [Index](README.md) | [Next](bibliography.md)

---

# GLOSSARY Glossary

Glossary.

<a id="GLS"></a>

A

abend

**abend**. (1) Abnormal end of task. (2) Synonym for *abnormal* *termination*.

abend dump

**abend** **dump**. The contents of main storage, or part of main storage, written to an external medium for debugging an error condition that resulted in the termination of a task before its regular completion.

abnormal end of task (abend)

**abnormal** **end** **of** **task** **(abend)**. Termination of a task before its completion because of an error condition that cannot be resolved by recovery facilities while the task is executing.

abnormal termination

**abnormal** **termination**. The ending of processing before planned termination. Synonymous with *abend*.

accept

**accept**. Allowing a connection to the user's virtual machine from another virtual machine or from the user's own virtual machine.

access mode

**access** **mode**. A method VM/ESA uses to control user access to data files. Access modes let the user read and write data to a file, or only read data from a file. See *file* *mode*.

access security

**access** **security**. Information that a target LU and target transaction program use to verify whether a source program is authorized to make a connection. This information consists of a user ID and, possibly, a password.

active work unit

**active** **work** **unit**. A work unit that has uncommitted work associated with it. A request was made on the work unit (other than an atomic request) and no commit or rollback has occurred.

advanced function printer (AFP)

**advanced** **function** **printer** **(AFP)**. An all-points-addressable printer, such as the IBM 3800-3 and IBM 3820 printers, capable of printing images and text.

Advanced Program-to-Program Communications (APPC)

**Advanced** **Program-to-Program** **Communications** **(APPC)**. The inter-program communication service within SNA LU 6.2 on which the APPC/VM interface is based.

Advanced Program-to-Program Communications/VM (APPC/VM)

**Advanced** **Program-to-Program** **Communications/VM** **(APPC/VM)**. An API for communicating between two virtual machines that is mappable to the SNA LU 6.2 APPC interface and based on IUCV functions. Along with the TASK virtual machine, AVS virtual machine, and VTAM; APPC/VM provides this communication within a single system, throughout a collection of systems, and throughout an SNA network.

AFP

**AFP**. Advanced function printer.

agent

**agent**. (1) In context of CRR sync point processing, agent is the role of the CRR sync point manager (SPM) when sync point requests are received from an initiator that is a partner in a protected APPC conversation. (2) In the context of an SFS file pool server or CRR recovery server, an agent represents a task. The SFS file pool server and CRR recovery server provide support for controlling multitasked execution for several agents.

alphanumeric

**alphanumeric**. A character set that contains letters, digits, and usually other characters, such as punctuation marks.

alternate console

**alternate** **console**. A console assigned as a backup unit to the system console.

American National Standard Code for Information Interchange (ASCII)

**American** **National** **Standard** **Code** **for** **Information** **Interchange** **(ASCII)**. The standard code, using a coded character set consisting of 7-bit coded characters (8 bits including parity check), used for information interchange among data processing systems, data communication systems, and associated equipment. The ASCII set consists of control characters and graphic characters.

AP

**AP**. Attached processor.

APAR

**APAR**. Authorized program analysis report.

APPC

**APPC**. Advanced Program-to-Program Communications.

APPC/VM

**APPC/VM**. Advanced Program-to-Program Communications/VM.

APPC/VM VTAM Support (AVS)

**APPC/VM** **VTAM** **Support** **(AVS)**. A component of VM/ESA that lets application programs using APPC/VM communicate with programs anywhere in a network defined by IBM's SNA. AVS transforms APPC/VM into APPC/VTAM protocol.

API

**API**. Application program interface.

application program

**application** **program**. A program written for or by a user that applies to the user's work, such as a program that does inventory control or payroll.

application program interface (API)

**application** **program** **interface** **(API)**. The formally defined programming language interface between an IBM system component or licensed program and its user.

apply

**apply**. When servicing a product or component, to generate an auxiliary control structure from a PTF.

area

**area**. A term acceptable for DASD space when there is no need to differentiate between space on count-key-data devices and FB-512 devices. See *DASD* *space*.

ASCII

**ASCII**. American National Standard Code for Information Interchange.

assembler language

**assembler** **language**. A source language that includes symbolic machine language statements in which there is a one-to-one correspondence with instruction formats and data formats of the computer.

attached processor (AP)

**attached** **processor** **(AP)**. A processor that has no I/O capability and is always linked to the processor initialized for I/O handling.

attention interrupt

**attention** **interrupt**. An I/O interrupt caused by a terminal user pressing the attention key (or equivalent). See *attention* *key* *(ATTN* *key)* and *signaling* *attention*.

attention key (ATTN key)

**attention** **key** **(ATTN** **key)**. A function key on terminals that, when pressed, causes an I/O interruption in the processing unit. See *signaling* *attention*.

ATTN key

**ATTN** **key**. Attention key.

authorized program analysis report (APAR)

**authorized** **program** **analysis** **report** **(APAR)**. An official request to the responsible IBM Change Team to look into a suspected problem with IBM code or documentation. APARs describe problems giving conditions of failure, error messages, abend codes, or other identifiers. They also contain a problem summary and resolution when applicable. See *program* *temporary* *fix* *(PTF)*.

authorized virtual machine

**authorized** **virtual** **machine**. A GCS virtual machine identified by user ID.

automatic software re-IPL

**automatic** **software** **re-IPL**. The process by which the control program attempts to restart the system after abnormal termination. This process does not involve the hardware IPL process. See *virtual=real* *machine* *recovery*.

auxiliary directory

**auxiliary** **directory**. In CMS, an extension of the CMS file directory for a minidisk, which contains the names and locations of certain CMS modules not included in the minidisk's CMS minidisk file directory.

auxiliary storage

**auxiliary** **storage**. Data storage other than main storage; in VM/ESA, auxiliary storage is usually a direct access device.

AVS

**AVS**. APPC/VM VTAM Support.

AVS virtual machine

**AVS** **virtual** **machine**. The virtual machine that manages a gateway that allows communication between VM systems and an SNA network..

<a id="GLS"></a>

B

backout

**backout**. The action taken by CRR for an application program to reverse the updates made to protected resources during a transaction (CRR logical unit of work). See *rollback*. The verb form of backout is back out.

basic control (BC) mode

**basic** **control** **(BC)** **mode**. A mode in which additional System/370 features, such as new machine instructions, are not operational. Contrast with *extended* *control* *(EC)* *mode*.

basic conversation

**basic** **conversation**. A conversation in which data is sent in APPC-defined logical record formats. Programs must be coded to consider the amount of data sent. Contrast with *mapped* *conversation*.

BC mode

**BC** **mode**. Basic control mode.

binary digit

**binary** **digit**. Either of the digits 0 or 1 when used in the pure binary numeration system. Synonymous with *bit*.

bit

**bit**. (1) Either of the binary digits 0 or 1. See *byte*. (2) Synonym for *binary* *digit*.

block

**block**. A unit of DASD space on FB-512 devices. For example, FB-512 devices can be the IBM 9335, 9332, 9313, 3370, and 3310 DASD using fixed-block architecture.

buffer

**buffer**. An area of storage, temporarily reserved for performing input or output, from which data is read, or into which data is written.

build

**build**. In the installation and service of a product, to do the necessary steps to produce executable code or systems. This is often called the *build* *process*.

byte

**byte**. A unit of storage, consisting of eight adjacent binary digits that are operated on as a unit and constitute the smallest addressable unit in the system..

<a id="GLS"></a>

C

callable services library (CSL)

**callable** **services** **library** **(CSL)**. A package of CMS assembler routines that can be stored as an entity and made available to application programs.

CAW

**CAW**. Channel address word.

CC

**CC**. Condition code.

CCS

**CCS**. Console communication service.

CCW

**CCW**. Channel command word.

changes

**changes**. In installation and service, IBM and original equipment manufacturer (OEM) supplied service for their programs. In the IBM service process, there are many ways users can receive information they need to fix (change) a portion(s) of a product they are running on a VM system. These include PTFs, APARs, user modifications, and information received over the phone. All these types of information are called *changes*.

channel

**channel**. A path in a system that connects a processor and main storage with an I/O device.

channel address word (CAW)

**channel** **address** **word** **(CAW)**. An area in storage that specifies the location in main storage at which a channel program begins.

channel command word (CCW)

**channel** **command** **word** **(CCW)**. A doubleword at the location in main storage specified by the channel address word. One or more CCWs make up the channel program that directs data channel operations.

channel status word (CSW)

**channel** **status** **word** **(CSW)**. An area in storage that provides information about the termination of I/O.

channel-to-channel adapter (CTCA)

**channel-to-channel** **adapter** **(CTCA)**. A hardware device that connects two channels on the same computing system or on different systems.

character delete symbol

**character** **delete** **symbol**. Synonym for *logical* *character* *delete* *symbol*.

checkpoint (CKPT) start

**checkpoint** **(CKPT)** **start**. A VM/ESA system restart that attempts to recover information about closed spool files previously stored on the checkpoint cylinders. The spool file chains are reconstructed, but the original sequence of spool files is lost. Unlike warm start, CP accounting and system message information is also lost. Contrast with *cold* *start,* *force* *start,* and *warm* *start*.

circumventive service

**circumventive** **service**. Information that IBM supplies over the phone or on a tape to circumvent a problem by disabling a failing function until a PTF is available to be shipped as a corrective service fix. See *patch* and *zap*.

CKD

**CKD**. Count-key-data.

class authority

**class** **authority**. Privilege assigned to a virtual machine user in the user's directory entry; each class specified allows access to a subset of all the CP commands. See *privilege* *class* and *user* *class* *restructure* *(UCR)*.

class B user

**class** **B** **user**. See *system* *resource* *operator* *privilege* *class*.

class G user

**class** **G** **user**. See *general* *user* *privilege* *class*.

CMS

**CMS**. Conversational Monitor System.

CMS EXEC

**CMS** **EXEC**. An EXEC procedure or EDIT macro written in the CMS EXEC language and processed by the CMS EXEC processor. Synonymous with *CMS* *program*.

CMS EXEC language

**CMS** **EXEC** **language**. A general-purpose, high-level programming language, particularly suitable for EXEC procedures and EDIT macros. The CMS EXEC processor executes procedures and macros (programs) written in this language. Contrast with *EXEC* *2* *language* and *Restructured* *Extended* *Executor* *(REXX)* *language*.

CMS file system

**CMS** **file** **system**. A way to create files in the CMS system. CMS files are created by using an identifier consisting of three fields: file name, file type, and file mode or SFS directory. These files are unique to the CMS system, cannot be read or written using other operating systems, and are stored either on minidisks or SFS directories.

CMS minidisk file directory

**CMS** **minidisk** **file** **directory**. A directory on each CMS disk that contains the name, format, size, and location of each of the CMS files on that disk. When a disk is accessed by the ACCESS command, its directory is read into virtual storage and identified with any letter from A through Z. Synonymous with *master* *file* *directory* *block* and *minidisk* *directory*.

CMS program

**CMS** **program**. Synonym for *CMS* *EXEC*.

cold start

**cold** **start**. A VM/ESA system restart that ignores previous data areas and accounting information in main storage, and the contents of paging and spool files on CP-owned disks. Contrast with *checkpoint* *(CKPT)* *start,* *force* *start,* and *warm* *start*.

collection

**collection**. See *TSAF* *collection*.

command

**command**. A request from a user at a terminal for the execution of a particular CP, CMS, IPCS, GCS, TSAF, or AVS function. A CMS command can also be the name of a CMS file with a file type of EXEC or MODULE. See *subcommand* and *user-written* *CMS* *command*.

command abbreviation

**command** **abbreviation**. A short form of the command name, operand, or option that is not a truncation of the word. For example, MSG instead of MESSAGE, RDR instead of READER. Contrast with *truncation*.

commit

**commit**. (1) In the context of SFS, permanently changing a resource (such as a file). (2) In the context of CRR, the action taken by CRR for an application program (or transaction program) to complete the updates made to protected resources (such as SFS file pools) during a transaction (CRR logical unit of work).

Common Programming Interface (CPI) Communications

**Common** **Programming** **Interface** **(CPI)** **Communications**. A set of program-to-program communication routines that let applications written in REXX and high-level languages access APPC/VM functions. These routines are part of IBM's Systems Application Architecture.

communication link

**communication** **link**. Synonym for *data* *link*.

communications directory

**communications** **directory**. A CMS facility that lets APPC/VM applications connect to a resource using symbolic destination names and special NAMES files.

communications partner

**communications** **partner**. The virtual machine on the other end of the local APPC/VM path, not necessarily the target of the communications.

communications server

**communications** **server**. A virtual machine that provides APPC/VM services between systems within a TSAF collection, and allows for communication between an APPC/VM environment and an SNA-defined network. TSAF and AVS are communication servers. Also known as an intermediate communications server.

component

**component**. A collection of objects that together form a separate functional unit. A product may contain many components. For example, CP, CMS, and TSAF are components of VM/ESA.

component override

**component** **override**. Synonym for *component* *parameter* *override*.

component parameter override

**component** **parameter** **override**. A component parameter, defined in a component override area, that updates or replaces a component parameter defined in a component area of the product parameter file. Synonymous with *component* *override* and *override*.

condition code (CC)

**condition** **code** **(CC)**. A code that reflects the result of a previous I/O, arithmetic, or logical operation.

connect

**connect**. Establishing a path to communicate with another virtual machine or with the user's own virtual machine.

console

**console**. A device used for communications between the operator or maintenance engineer and the computer.

console communication service (CCS)

**console** **communication** **service** **(CCS)**. A group of CP modules that interfaces with the VTAM service machine, providing full VM/ESA console capabilities for SNA terminal users.

console function

**console** **function**. The subset of CP commands that lets the user simulate almost all of the functions available to an operator at a real system console.

console spooling

**console** **spooling**. Synonym for *virtual* *console* *spooling*.

contention

**contention**. The situation where two LUs try to allocate a conversation over the same session at the same time.

control block

**control** **block**. A storage area that a computer program uses to hold control information.

control program

**control** **program**. A computer program that schedules and supervises the program execution in a computer system. See *Control* *Program* *(CP)* and *Control* *Program* *370* *(CP370)*.

Control Program (CP)

**Control** **Program** **(CP)**. A component of the ESA Feature of VM/ESA that manages the resources of a single computer so multiple computing systems appear to exist. Each virtual machine is the functional equivalent of an IBM System/370 or System/390.

Control Program 370 (CP370)

**Control** **Program** **370** **(CP370)**. A component of the 370 Feature of VM/ESA that manages the resources of a single computer so multiple computing systems appear to exist. Each virtual machine is the functional equivalent of an IBM System/370.

control section (CSECT)

**control** **section** **(CSECT)**. The part of a program specified by the programmer to be a relocatable unit, all elements of which are loaded into adjoining main storage.

control statement

**control** **statement**. A statement that controls or affects program execution in a data processing system.

control unit

**control** **unit**. A device that controls I/O operations at one or more devices.

conversation

**conversation**. A connection between two transaction programs over an LU-LU session that lets them communicate with each other while processing some transaction. The programs establish a conversation, send and receive data in the conversation, and then terminate the conversation.

Conversational Monitor System (CMS)

**Conversational** **Monitor** **System** **(CMS)**. A virtual machine operating system and component of VM/ESA that provides general interactive time sharing, problem solving, program development capabilities, and operates only under the control of the VM Control Program (CP).

conversation correlator

**conversation** **correlator**. A value that identifies an APPC conversation and is unique at the LU that generates it. The conversation correlator is established when the APPC conversation is established.

conversation state

**conversation** **state**. See *program* *state*.

Coordinated Resource Recovery (CRR)

**Coordinated** **Resource** **Recovery** **(CRR)**. A CMS facility that implements the LU 6.2 sync point architecture, which ensures that transactions can update multiple protected resources with integrity. This means that all updates, within the transaction, are either completed (committed) or not completed (rolled back or backed out). CRR consists of the coordination function (see *synchronization* *point* *processing*), the resynchronization function (see *resynchronization*), and the logging function (see *log* *minidisks*). The coordination function resides in the application program's virtual machine. The resynchronization and logging functions reside in the CRR recovery server.

copy file

**copy** **file**. A file having file type COPY that contains nonexecutable real storage definitions that are referred to by macros and assemble files.

corrective service

**corrective** **service**. Service that IBM supplies on tape to correct a specific problem.

count-key-data (CKD) device

**count-key-data** **(CKD)** **device**. A disk storage device that stores data in the format: count field, usually followed by a key field, followed by the actual data of a record. The count field contains the cylinder number, head number, record number, and the length of the data. The key field contains the record's key (search argument).

CP

**CP**. Control Program.

CP370

**CP370**. Control Program 370.

CP assist

**CP** **assist**. A hardware function, available only on a processor with ECPS, that reduces CP overhead by doing the most frequently used tasks of CP routines.

CP command

**CP** **command**. A command available to all VM users. Class G CP commands let the general user reconfigure their virtual machine, control devices attached to their virtual machine, do input and output spooling functions, and simulate many other functions of a real computer console. Other CP commands let system operators, system programmers, system analysts, and service representatives manage the resources of the system.

CP directory

**CP** **directory**. Synonym for *VM* *directory*.

CPI Communications

**CPI** **Communications**. Common Programming Interface Communications.

CP read

**CP** **read**. The condition when CP is waiting for a response or request for work from the user. On a typewriter terminal, the keyboard is unlocked; on a display terminal, the screen status area indicates CP READ.

CP trace table

**CP** **trace** **table**. A table VM/ESA uses for debugging. Its size is a multiple of 4096 bytes and depends on the size of real storage or a user specified value. This table contains the chronological occurrences of events that take place in the real machine, recorded in a wraparound fashion within the trace table. Synonymous with *trace* *table*.

CRR

**CRR**. Coordinated Resource Recovery.

CSECT

**CSECT**. Control section.

CSL

**CSL**. Callable services library.

CSW

**CSW**. Channel status word.

CTCA

**CTCA**. Channel-to-channel adapter.

cylinder

**cylinder**. In a disk pack, the set of all tracks with the same nominal distance from the axis about which the disk pack rotates..

<a id="GLS"></a>

D

DASD

**DASD**. Direct access storage device.

DASD space

**DASD** **space**. (1) Area allocated to DASD units on CKD devices. (2) Area allocated to DASD units on FB-512 devices. Note that *DASD* *space* is synonymous with *cylinder* when there is no need to differentiate between CKD devices and FB-512 devices.

data link

**data** **link**. The equipment and rules (protocols) used for sending and receiving data. Synonymous with *communication* *link*.

data stream

**data** **stream**. A set of logical records sent one after the other.

DCSS

**DCSS**. Discontiguous saved segment.

dedicated device

**dedicated** **device**. An I/O device or line not being shared among users. The facility can be permanently assigned to a particular virtual machine by a VM/ESA directory entry, or temporarily attached by the resource operator to the user's virtual machine.

delimiter

**delimiter**. (1) A flag that separates and organizes items of data. Synonymous with *separator*. (2) A character that groups or separates words or values in a line of input. Usually one or more blank characters separate the command name and each operand or option in the command line. In certain cases, a tab, left parenthesis, or backspace character can also act as a delimiter.

device support facilities

**device** **support** **facilities**. A program for doing operations on disk volumes so that they can be accessed by IBM and user programs. Examples of these operations are initializing a disk volume and assigning an alternate track.

DIAGNOSE interface

**DIAGNOSE** **interface**. A programming mechanism that lets any virtual machine, including CMS, directly communicate with CP by way of the DIAGNOSE instruction. Specific interface codes let a virtual machine more efficiently request specific CP services.

DIRCONTROL directory

**DIRCONTROL** **directory**. Synonym for *directory* *control* *directory*.

direct access storage device (DASD)

**direct** **access** **storage** **device** **(DASD)**. A storage device in which the access time is effectively independent of the location of the data.

directory

**directory**. See *auxiliary* *directory,* *CMS* *minidisk* *file* *directory* *DIRCONTROL* *directory,* *directory* *control* *directory,* *file* *control* *directory* *FILECONTROL* *directory,* *SFS* *directory,* or *VM* *directory*.

directory control directory

**directory** **control** **directory**. A type of SFS directory with functional characteristics like a minidisk's. A single access authority applies to the directory and all the files in the directory. When you access a directory control directory in read-only mode, you cannot see changes made until you release and reaccess the directory. When you access the directory in read/write mode, changes become available as they are made. Synonymous with *DIRCONTROL* *directory*. Contrast with *file* *control* *directory*.

discontiguous saved segment

**discontiguous** **saved** **segment**. One or more segments of storage that were previously loaded, saved, and assigned a unique name. In VM/ESA (ESA Feature), a segment begins and ends on a 1MB boundary. In VM/ESA (370 Feature), a segment begins and ends on a 64KB boundary. The segment(s) can be shared among virtual machines if the segment(s) contains reentrant code. Discontiguous segments used with CMS must be loaded into storage at locations above the address space of a user's CMS virtual machine. They can be detached when no longer needed.

disk

**disk**. A magnetic disk unit in the user's CMS virtual machine configuration. Also called a virtual disk.

disk operating system (DOS)

**disk** **operating** **system** **(DOS)**. An operating system for computer systems that use disks and diskettes for auxiliary storage of programs and data.

Disk Operating System/Virtual Storage Extended (DOS/VSE)

**Disk** **Operating** **System/Virtual** **Storage** **Extended** **(DOS/VSE)**. An operating system that is an extension of DOS/VS. A VSE system consists of: (a) licensed VSE/Advanced Functions support, and (b) any IBM-supplied and user-written programs required to meet the data processing needs of a user. VSE and the hardware it controls form a complete computing system.

dispatching

**dispatching**. The starting of virtual machine execution.

dispatch list

**dispatch** **list**. See *run* *list*.

display device

**display** **device**. An I/O device that gives a visual representation of data.

display mode

**display** **mode**. A type of editing at a display terminal in which an entire screen of data is displayed at once and in which the user can access data through commands or by using a cursor. Contrast with *line* *mode*.

distribution code

**distribution** **code**. In the VM/ESA directory, a one-to-eight character identification word printed or punched with the user ID in the separator page (or punched card) to further identify the location or department of the user.

Document Composition Facility (DCF)

**Document** **Composition** **Facility** **(DCF)**. A text processing program; its main component is the text formatter, called SCRIPT/VS. See *SCRIPT/VS*.

DOS

**DOS**. Disk operating system.

DOS/VSE

**DOS/VSE**. Disk Operating System/Virtual Storage Extended.

dump

**dump**. To write the contents of part or all of main storage, or part or all of a minidisk, to auxiliary storage or a printer. See *abend* *dump*.

dump viewing facility

**dump** **viewing** **facility**. An ESA Feature component that lets users display, format, and print data interactively from CP hard and soft abend, stand-alone, and virtual machine dumps, process CP trace table data stored on tape or in a system trace file, and display symptom records.

dynamic paging area (DPA)

**dynamic** **paging** **area** **(DPA)**. An area of real storage that CP uses for virtual machine pages and pageable CP modules..

<a id="GLS"></a>

E

EBCDIC

**EBCDIC**. Extended binary-coded decimal interchange code.

EC mode

**EC** **mode**. Extended control mode.

ECPS: VM/370

**ECPS:****VM/370**. Extended Control Program Support:VM/370.

EDF

**EDF**. Enhanced disk format.

edit

**edit**. A function that makes changes, additions, or deletions to a file on a disk. These changes are interactively made. The edit function also generates information in a file that did not previously exist.

eligible list

**eligible** **list**. The list of virtual machines waiting to get into the run list. They are runable but cannot fit into the run list because of the current system load.

emulation

**emulation**. The use of programming techniques and special machine features to permit a computing system to execute programs written for another system.

emulation program (EP)

**emulation** **program** **(EP)**. A control program that lets an IBM 3704 or 3705 Communications Controller emulate the functions of an IBM 2701 Data Adapter Unit, an IBM 2702 Transmission Control Unit, or an IBM 2703 Transmission Control Unit.

enhanced disk format (EDF)

**enhanced** **disk** **format** **(EDF)**. A CMS file storage format that supports files consisting of 512-, 1K-, 2K-, or 4K-byte CMS blocks.

entry point

**entry** **point**. An address or label of an instruction performed on entering a computer program, a routine, or a subroutine. A program can have several different entry points, each corresponding to a different function or purpose.

Environmental record editing and printing program (EREP)

**Environmental** **record** **editing** **and** **printing** **program** **(EREP)**. A program that makes the data contained in the system recorder file available for further analysis.

EOF

**EOF**. End of file.

EP

**EP**. Emulation program.

EREP

**EREP**. Environmental record editing and printing program.

ESA/370 mode

**ESA/370** **mode**. A virtual machine operating mode in which ESA/370 functions are simulated. Contrast with *System/370* *mode,* *370-XA* *mode,* and *ESA/390* *mode*.

ESA/390 mode

**ESA/390** **mode**. A virtual machine operating mode in which ESA/390 functions are simulated. Contrast with *System/370* *mode,* *370-XA* *mode,* and *ESA/370* *mode*.

escape symbol

**escape** **symbol**. Synonym for *logical* *escape* *symbol*.

EXEC 2 language

**EXEC** **2** **language**. A general-purpose, high-level programming language, particularly suitable for EXEC procedures and XEDIT macros. The EXEC 2 processor runs procedures and XEDIT macros (programs) written in this language. Contrast with *CMS* *EXEC* *language* and *Restructured* *Extended* *Executor* *(REXX)* *language*.

expanded storage

**expanded** **storage**. Optional integrated high-speed storage. On the ESA Feature, Expanded Storage can be shared by CP and one or more virtual machines. It can also be dedicated to CP or to a particular virtual machine.

expanded virtual machine assist

**expanded** **virtual** **machine** **assist**. A hardware assist function, available only on a processor that has ECPS, that handles many privileged instructions not handled by VMA, and extends the level of support of certain privileged instructions beyond that provided by VMA.

extended binary-coded decimal interchange code (EBCDIC)

**extended** **binary-coded** **decimal** **interchange** **code** **(EBCDIC)**. A set of 256 characters, with each character represented by 8 bits.

extended control (EC) mode

**extended** **control** **(EC)** **mode**. A mode in which all features of a System/370 computing system, including dynamic address translation, are operational. Contrast with *basic* *control* *(BC)* *mode*.

Extended Control Program Support (ECPS: VM/370)

**Extended** **Control** **Program** **Support** **(ECPS:****VM/370)**. A hardware assist feature that improves the performance of CP by reducing CP overhead. ECPS:VM/370 consists of CP assist, expanded virtual machine assist, and virtual interval timer assist.

external security manager

**external** **security** **manager**. A program that either augments or completely replaces the authorization checking done by file pool server processing..

<a id="GLS"></a>

F

FCB

**FCB**. (1) Forms control buffer. (2) Function control block.

fetch protection

**fetch** **protection**. A storage protection feature that determines right-of-access to main storage by matching the protection key associated with a main storage fetch reference with the storage keys associated with those frames of main storage.

file access mode

**file** **access** **mode**. A file mode number that designates whether the file can be used as a read-only or read/write file by a user. See *file* *mode*.

file control directory

**file** **control** **directory**. A type of SFS directory for which separate access authorities are granted to the directory and to the individual files in the directory. When you access a file control directory, changes to the directory become available as they are made. Synonymous with *FILECONTROL* *directory*. Contrast with *directory* *control* *directory*.

FILECONTROL directory

**FILECONTROL** **directory**. Synonym for *file* *control* *directory*.

file ID

**file** **ID**. A CMS file identifier that consists of a file name, file type, and file mode. The file ID is associated with a particular file when the file is created, defined, or renamed under CMS. See *file* *name,* *file* *type,* and *file* *mode*.

file mode

**file** **mode**. A two-character CMS file identifier field containing the file mode letter (A through Z) followed by the file mode number (0 through 6). The file mode letter indicates the minidisk or SFS directory on which the file resides. The file mode number indicates the access mode of the file. See *file* *access* *mode*.

file name

**file** **name**. A one-to-eight character alphanumeric field, containing A through Z, 0 through 9, and special characters $ # @ + - (hyphen) (colon) _ (underscore), that is part of the CMS file identifier and serves to identify the file for the user.

file type

**file** **type**. A one-to-eight character alphanumeric field, containing A through Z, 0 through 9, and special characters $ # @ + - (hyphen) (colon) _ (underscore), that is used as a descriptor or as a qualifier of the file name field in the CMS file identifier. See *reserved* *file* *types*.

first-level storage

**first-level** **storage**. Refers to real main storage. Contrast with *second-level* *storage* and *third-level* *storage*.

FMH5

**FMH5**. Function Management Header 5.

force start

**force** **start**. A VM/ESA system restart that attempts to recover information about closed spool files previously stored on the checkpoint cylinders. All unreadable or incorrect spool file information is ignored. Contrast with *checkpoint* *(CKPT)* *start,* *cold* *start,* and *warm* *start*.

forms control buffer (FCB)

**forms** **control** **buffer** **(FCB)**. In the 3800 Printing Subsystem, a buffer for controlling the vertical format of printed output. The FCB is analogous to the punched-paper, carriage-control tape that IBM 1403 Printers use.

free storage

**free** **storage**. Storage not allocated. The blocks of central storage available for temporary use by programs or by the system.

full-pack minidisk

**full-pack** **minidisk**. A virtual disk that contains all of the addressable cylinders of a real DASD volume.

full-screen CMS

**full-screen** **CMS**. When a user enters the command SET FULLSCREEN ON, CMS is in a window and can take advantage of 3270-type architecture and windowing support, and various classes of output are routed to a set of default windows. Also, users can type commands anywhere on the physical screen and scroll through commands and responses previously displayed. See *windowing*.

full-screen mode

**full-screen** **mode**. In VM, the environment in which an entire 3270 display screen is under the control of a program running in a virtual machine.

fully-qualified LU name

**fully-qualified** **LU** **name**. A name that identifies each LU in an SNA network. It consists of a network ID followed by a network LU name. Contrast with *locally-known* *LU* *name*.

function control block (FCB)

**function** **control** **block** **(FCB)**. In Subsystem Support Services (SSS), a control block that contains information such as a function's status, event control block, task I/O queue, and I/O queue.

Function Management Header 5 (FMH5)

**Function** **Management** **Header** **5** **(FMH5)**. A field at the beginning of an application request that carries control information for the target LU in an SNA network..

<a id="GLS"></a>

G

gateway

**gateway**. The LU name of a VM system or TSAF collection that is a source for communications to an SNA-defined network or the target of communications from an SNA-defined network.

gateway manager

**gateway** **manager**. A virtual machine in which one or more gateways are active. AVS is a gateway manager.

GCS

**GCS**. Group Control System for ESA/370 or ESA/390 architecture.

GCS370

**GCS370**. Group Control System for System/370 architecture.

general register

**general** **register**. In CMS, a register that does operations such as binary addition, subtraction, multiplication, and division. General registers primarily compute and modify addresses in a program.

general user privilege class

**general** **user** **privilege** **class**. The subset of CP commands that lets the Class G user manipulate and control a virtual machine.

global resource

**global** **resource**. A resource accessible from anywhere within a TSAF collection and whose identity is known throughout the collection. A shared file system file pool is an example of a global resource. Contrast with *local* *resource* and *private* *resource*.

global resource manager

**global** **resource** **manager**. An application that runs in a server virtual machine and identifies itself to the TSAF collection as a global resource owner using *IDENT. Contrast with *local* *resource* *manager* and *private* *resource* *manager*.

group

**group**. Synonym for *virtual* *machine* *group*.

Group Control System (GCS)

**Group** **Control** **System** **(GCS)**. A component of VM/ESA, consisting of a shared segment that the user can IPL and run in a virtual machine. It provides simulated MVS services and unique supervisor services to help support a native SNA network.

Group Control System (GCS370)

**Group** **Control** **System** **(GCS370)**. The component of the 370 Feature of VM that, as a virtual machine supervisor, executes in a group of System/370 virtual machines under CP370 control. This provides an interface that helps support a native SNA network.

guest

**guest**. An operating system running in a virtual machine managed by a VM control program. Contrast with *host*.

guest real storage

**guest** **real** **storage**. The storage that appears real to the operating system running in a virtual machine. Contrast with *guest* *virtual* *storage*, *host* *real* *storage*, and *host* *virtual* *storage*.

guest virtual machine (GVM)

**guest** **virtual** **machine** **(GVM)**. A virtual machine in which an operating machine is running.

guest virtual storage

**guest** **virtual** **storage**. The storage that appears virtual to the operating system running in a virtual machine. Contrast with *guest* *real* *storage*, *host* *real* *storage*, and *host* *virtual* *storage*..

<a id="GLS"></a>

H

half-duplex protocol

**half-duplex** **protocol**. A communications protocol where only one communication partner can send data at a given time.

host

**host**. A VM control program in its capacity as manager of a virtual machine in which another operating system is running. Contrast with *guest*.

host real storage

**host** **real** **storage**. The storage that appears real to the control program. If VM is running native, this is real storage; if VM is running in a virtual machine, this is virtual storage. Contrast with *guest* *real* *storage*, *guest* *virtual* *storage*, and *host* *virtual* *storage*.

host virtual storage

**host** **virtual** **storage**. The storage that appears virtual to the control program. Contrast with *guest* *real* *storage*, *guest* *virtual* *storage*, and *host* *real* *storage*..

<a id="GLS"></a>

I

ID card

**ID** **card**. Under VM/ESA, the identification card that indicates the destination user ID of a deck of real cards. These cards are read into the system card reader or into the card reader of an RSCS remote station.

image library

**image** **library**. A set of modules that define the spacing, characters, and copy modification data that a 3800 printer uses to print a spool file or that define the spacing and character set that an impact printer uses to print a spool file. See *system* *data* *file*.

inactive work unit

**inactive** **work** **unit**. A work unit on which no requests have yet been made, or an atomic request was made, or requests were made and have been committed or rolled back; that is, an inactive work unit has no uncommitted work associated with it.

indicator

**indicator**. A 1-byte area of storage that contains either the character "1" to denote a true condition or the character "0" to denote a false condition.

in-doubt

**in-doubt**. A protected resource is called in-doubt when it has successfully completed the first phase of the two-phase commit and it is waiting for a decision from the initiator to either commit or roll back the changes and therefore start the second phase of the two-phase commit.

initial program load (IPL)

**initial** **program** **load** **(IPL)**. The initialization procedure that causes an operating system to begin operation. A VM user must IPL the specific operating system into the virtual machine that will control the user's work. Each virtual machine can be loaded with a different operating system.

initialize

**initialize**. To set counters, switches, addresses, or contents of storage to starting values.

input/output (I/O)

**input/output** **(I/O)**. (1) A device whose parts can do an input process and an output process at the same time. (2) A functional unit or channel involved in an input process, output process, or both, concurrently or not, and to the data involved in such a process.

interaction

**interaction**. A basic unit that records system activity, consisting of acceptance of a line of terminal input, processing of the line, and a response, if any.

interactive

**interactive**. The classification given to a virtual machine depending on this virtual machine's processing characteristics. When a virtual machine uses less than its allocation time slice because of terminal I/O, the virtual machine is classified as being interactive. Contrast with *noninteractive*.

Interactive Problem Control System (IPCS)

**Interactive** **Problem** **Control** **System** **(IPCS)**. A component of VM/ESA (370 Feature) that permits online problem management, interactive problem diagnosis, online debugging for disk related CP or virtual machine abend dumps or CPTRAP files, problem tracking, and problem reporting.

interface

**interface**. A shared boundary between two or more entities. An interface might be a hardware or software component that links two devices or programs together.

intermediate communications server

**intermediate** **communications** **server**. A virtual machine that handles communications requests to a resource manager program for a user program.

interrupt

**interrupt**. A suspension of a process, such as execution of a computer program, caused by an external event and done in such a way that the process can be resumed.

inter-user communication vehicle (IUCV)

**inter-user** **communication** **vehicle** **(IUCV)**. A VM/ESA generalized CP interface that helps the transfer of messages either among virtual machines or between CP and a virtual machine.

invoke

**invoke**. To start a command, procedure, or program.

I/O

**I/O**. Input/output.

IPCS

**IPCS**. Interactive Problem Control System.

IPL

**IPL**. Initial program load.

IPL processor

**IPL** **processor**. In an AP or MP system, the processor on which the control program was first initialized during system generation. Note that both the IPL and the non-IPL processors in a real MP configuration have I/O capabilities.

IUCV

**IUCV**. Inter-user communication vehicle..

<a id="GLS"></a>

L

line delete symbol

**line** **delete** **symbol**. Synonym for *logical* *line* *delete* *symbol*.

line deletion symbol

**line** **deletion** **symbol**. Synonym for *logical* *line* *delete* *symbol*.

line end symbol

**line** **end** **symbol**. Synonym for *logical* *line* *end* *symbol*.

line mode

**line** **mode**. The mode of operation of a display terminal that is equivalent to using a typewriter-like terminal. Contrast with *display* *mode*.

link

**link**. (1) In RSCS, a connection, or ability to communicate, between two adjacent nodes in a network. (2) In TSAF, the physical connection between two systems.

load

**load**. In installation and service, to move files from tape to disk, auxiliary storage to main storage, or minidisks to virtual storage within a virtual machine.

loader

**loader**. A routine, commonly a computer program, that reads data into main storage.

local

**local**. Two entities (for example, a user and a server) are said to be local to each other if they belong to the same system within a collection or to the same node within an SNA system. Contrast with *remote*.

locally-known LU name

**locally-known** **LU** **name**. An LU name that transaction programs use to identify a remote (target) LU in the SNA network. Contrast with *fully-qualified* *LU* *name*.

local resource

**local** **resource**. A resource accessible from only within a single VM system and whose identity is known only within a single VM system in the TSAF collection. Contrast with *global* *resource* and *private* *resource*.

local resource manager

**local** **resource** **manager**. An application that runs in a virtual machine and identifies itself to the local system in the TSAF collection as a local resource owner by *IDENT. Contrast with *global* *resource* *manager* and *private* *resource* *manager*.

local service

**local** **service**. Changes manually applied to a product or component (that is, not using the program update service or corrective service procedures). See *circumventive* *service* and *user* *modification*.

lock

**lock**. A tool for controlling concurrent usage of SFS objects. Implicit locks are acquired and automatically released when you run CMS commands and program functions in SFS. Explicit locks let you control the type and duration of the lock.

log data

**log** **data**. Information that a communications program can send to its partner to help diagnose errors.

logical character delete symbol

**logical** **character** **delete** **symbol**. A special editing symbol, usually the *at* (@) sign, that causes CP to delete it and the immediately preceding character from the input line. If many delete symbols are consecutively entered, that same number of preceding characters are deleted from the input line. The value can be redefined or unassigned by the installation or the user. Synonymous with *character* *delete* *symbol*.

logical editing symbols

**logical** **editing** **symbols**. Symbols that let the user correct entering errors, combine multiple lines of input on one physical line, and enter logical editing symbols as data. The logical editing symbols can be defined, reassigned, or unassigned by the user. See *logical* *character* *delete* *symbol,* *logical* *escape* *symbol,* *logical* *line* *delete* *symbol*, and *logical* *line* *end* *symbol*.

logical escape symbol

**logical** **escape** **symbol**. A special editing symbol, usually the double quotation (") symbol, that causes CP to consider the immediately following character as a data character instead of as a logical editing symbol. Synonymous with *escape* *symbol*.

logical line

**logical** **line**. A command or data line that can be separated from one or more additional command or data lines on the same input line by a logical line end symbol.

logical line delete symbol

**logical** **line** **delete** **symbol**. A special editing symbol, usually the cent (¢) sign, that causes CP to delete the previous logical line in the input line back to and including the previous logical line end symbol. Synonymous with *line* *delete* *symbol* and *line* *deletion* *symbol*. See *logical* *line*.

logical line end symbol

**logical** **line** **end** **symbol**. A special editing symbol, usually the pound (#) sign, that lets the user enter the equivalent of several command or data lines in the same physical line; that is, each logical line except the last line is terminated with the logical line end symbol. Synonymous with *line* *end* *symbol*.

logical record

**logical** **record**. A formatted record that consists of a 2-byte logical record length and a data field of variable length.

logical saved segment

**logical** **saved** **segment**. A portion of a physical saved segment that CMS can manipulate. Each logical segment can contain different types of program objects, such as modules, text files, execs, callable services libraries, language repositories, user-defined objects, or a single minidisk directory. A system segment identification file (SYSTEM SEGID) associates a logical saved segment to the physical saved segment in which it resides. See *physical* *saved* *segment* and *saved* *segment*.

logical unit (LU)

**logical** **unit** **(LU)**. An entity addressable within an SNA-defined network, similar to a node within a VM network. LUs are categorized by the types of communication they support. A TSAF collection in an SNA network is viewed as one or more LUs.

logical unit name (LU name)

**logical** **unit** **name** **(LU** **name)**. A symbolic name given to a particular LU in an SNA-defined network.

logical unit of work

**logical** **unit** **of** **work**. (1) In SFS, a group of related operations that the SFS file pool server is doing for a user. The operations in a logical unit of work can either be committed or rolled back as a unit. Sometimes this is called a resource logical unit of work. (2) In CRR, a logical unit of work is a convenient abstraction for the application processing (including the underlying system support) performed to take a set of protected resources (such as SFS file pools) from one consistent state to another (commit changes) in such a way that the unit of work appears atomic. If a failure occurs during the sync point processing of a logical unit of work, any changes made by (or for) the logical unit of work are rolled back, so that the protected resources are returned to their previous consistent state. A CRR logical unit of work is frequently called a *transaction* or *LUWID* and consists of one or more *LUWID* *instances*.

logical unit of work identifier (LUWID)

**logical** **unit** **of** **work** **identifier** **(LUWID)**. The identifier of a CRR logical unit of work. The LUWID includes three parts: the fully qualified LU network name; the instance number, which is unique at the LU that creates it; and the sequence number, which is incremented by one following a sync point. Also, the conversation correlator is used to further qualify LUWIDs.

log minidisks

**log** **minidisks**. (1) In SFS, two duplicate minidisks that contain information about changes made to the file pool. SFS file pool servers use the SFS log minidisks to help protect the integrity of the file pool if a system failure occurs. (2) In CRR, two duplicate minidisks that contain information about the states of various logical units of work during sync point processing. CRR recovery servers use the CRR log minidisks to perform resynchronization processing to protect the integrity of the transaction if a failure occurs during sync point processing.

logoff

**logoff**. The procedure by which a user ends a terminal session.

logon

**logon**. The procedure by which a user begins a terminal session.

LU

**LU**. Logical unit.

LUWID

**LUWID**. Logical unit of work identifier.

LU name

**LU** **name**. Logical unit name.

LU type 6.2

**LU** **type** **6.2**. A set of protocols and services defined by IBM's SNA for communication between application programs..

<a id="GLS"></a>

M

machine

**machine**. A synonym for a virtual machine running under the control of VM/ESA.

macro

**macro**. Synonym for *macrodefinition* and *macroinstruction*.

macrodefinition

**macrodefinition**. A set of statements that defines the name of, format of, and conditions for generating a sequence of assembler language statements from a single source statement. Synonymous with *macro*.

macroinstruction

**macroinstruction**. In assembler language programming, an assembler language statement that causes the assembler to process a predefined set of statements called a macrodefinition. The statements usually produced from the macrodefinition replace the macroinstruction in the program. Synonymous with *macro*.

macro library

**macro** **library**. A library of macrodefinitions.

map

**map**. In CMS, the file that contains a CMS output listing, such as (1) a list of macros in the MACLIB library, including macro size and location within the library, (2) a listing of the directory entries for the DOS/VS system or private source, relocatable, or core image libraries, (3) a linkage editor map for CMS/DOS programs, and (4) a module map containing entry point locations.

mapped conversation

**mapped** **conversation**. A conversation where data is sent in arbitrary length buffers. Programs do not have to be concerned with the format of data being sent. Contrast with *basic* *conversation*.

master file directory block

**master** **file** **directory** **block**. Synonym for *CMS* *minidisk* *file* *directory*.

MB

**MB**. Megabyte.

MDISK

**MDISK**. (1) Another name for minidisk. (2) The user directory that describes a user's storage space.

member saved segment

**member** **saved** **segment**. A saved segment that begins and ends on a page boundary. It can be a member in up to 64 segment spaces and is accessed either by the segment space name or by its own name. Contrast with *discontiguous* *saved* *segment*. See *saved* *segment*, *segment*, and *segment* *space*.

megabyte (MB)

**megabyte** **(MB)**. 1,048,576 bytes.

merge

**merge**. When receiving files from a service tape using the VMFREC EXEC, the process of moving existing service files from each minidisk or SFS directory in the target string (as defined by the MERGE tag in the product parameter file) to the minidisk or SFS directory that contains the previous service level. The result is that the primary target minidisk or directory is left empty and ready to receive the latest service from the tape.

message

**message**. Data sent from a source application to a target application program in a conversation.

message repository

**message** **repository**. A source file that contains message texts for a VM component or user application. It is compiled into internal form by the GENMSG command. The message text in a repository file can be translated and used to support national languages.

minidisk

**minidisk**. A logical subdivision (or all) of a physical disk pack that has its own virtual device address, consecutive virtual cylinders (starting with virtual cylinder 0), and a VTOC or disk label identifier. Each user virtual disk is preallocated and defined by a VM/ESA directory entry as belonging to a user.

minidisk directory

**minidisk** **directory**. Synonym for *CMS* *minidisk* *file* *directory*.

mode name

**mode** **name**. A symbolic name given to a set of characteristics that describe a particular LU 6.2 session.

module

**module**. (1) A unit of a software product that is discretely and separately identifiable with respect to modifying, compiling, and merging with other units, or with respect to loading and execution. For example, the input to, or output from, a compiler, the assembler, the linkage editor, or an exec routine. (2) A nonrelocatable file whose external references have been resolved.

MP

**MP**. Multiprocessor.

multiple preferred guests

**multiple** **preferred** **guests**. A facility provided by the ESA Feature of VM that supports up to six preferred virtual machines when the Processor Resource/Systems Manager (PR/SM) feature is installed in the real machine. See *preferred* *virtual* *machine*.

Multiple Virtual Storage (MVS)

**Multiple** **Virtual** **Storage** **(MVS)**. An alternative name for OS/VS2.

multiprocessor (MP)

**multiprocessor** **(MP)**. A computer using two or more processing units under integrated control.

MVS

**MVS**. Multiple Virtual Storage..

<a id="GLS"></a>

N

named saved system (NSS)

**named** **saved** **system** **(NSS)**. A copy of an operating system that a user has named and saved in a file. The user can load the operating system by its name, which is more efficient than loading it by device number. See *discontiguous* *saved* *segment*, *member* *saved* *segment*, *saved* *segment*, *segment* *space*, and *system* *data* *file*.

network

**network**. Any set of two or more computers, workstations, or printers linked in such a way as to let data be transmitted between them.

node

**node**. (1) A single processor or a group of processors in a teleprocessing network. (2) A computer, workstation, or printer, when it is participating in a network.

node ID

**node** **ID**. Node identifier.

node identifier (node ID)

**node** **identifier** **(node** **ID)**. The name by which a node is known to all other nodes in a network.

noninteractive

**noninteractive**. The classification given to a virtual machine depending on the virtual machine's processing characteristics. When a virtual machine usually uses all its allocated queue slice, it is classified as being noninteractive or compute bound. Contrast with *interactive*.

NSS

**NSS**. Named saved system..

<a id="GLS"></a>

O

operand

**operand**. Information entered with a command name to define the data on which a command processor operates and to control the execution of the command processor.

overhead

**overhead**. The additional processor time charged to each virtual machine for the CP functions needed to simulate the virtual machine environment and for paging and scheduling time.

overlay

**overlay**. The technique of repeatedly using the same areas of internal storage during different stages of a program.

override

**override**. Synonym for *component* *parameter* *override*..

<a id="GLS"></a>

P

pack

**pack**. A set of flat, circular recording surfaces that a disk storage device uses. A disk pack.

page

**page**. A fixed-length block that has a virtual address and can be transferred between real storage and auxiliary storage.

page frame

**page** **frame**. A block of 4096 bytes of real storage that holds a page of virtual storage.

page locking

**page** **locking**. Marking a page as nonpageable so that it remains in real storage until released.

page number

**page** **number**. The part of a virtual storage address needed to refer to a page.

page zero

**page** **zero**. Storage locations 0 to 4095.

paging

**paging**. Transferring pages between real storage and external page storage.

paging area

**paging** **area**. An area of direct access storage (and an associated area of real storage) that CP uses for the temporary storage of pages when paging occurs.

parameter

**parameter**. A variable that is given a constant value for a specified application and that may denote the application.

parameter list (PLIST)

**parameter** **list** **(PLIST)**. In CMS, a string of 8-byte arguments that call a CMS command or function. The first argument must be the name of the command or function to be called. General register 1 points to the beginning of the parameter list.

part

**part**. A CMS file provided on a product tape or service tape as input to the build process. See *build*. A part is the smallest serviceable unit of a component.

password

**password**. In computer security, a string of characters known to the computer system and a user, who must specify it to gain full or limited access to a system and to gain full or limited access to a system and to the data stored within it.

patch

**patch**. A circumventive service change applied directly to object code in a text deck in a nucleus.

path

**path**. In APPC/VM or IUCV, a connection between two application programs that are on the same or different systems. Paths have names assigned to them.

PF key

**PF** **key**. Programmed function key.

physical saved segment

**physical** **saved** **segment**. One or more pages of storage that have been named and retained on a CP-owned volume (DASD). Once created, it can be loaded within a virtual machine's address space or outside a virtual machine's address space. Multiple users can load the same copy. A physical saved segment can contain one or more logical saved segments. A system segment identification file (SYSTEM SEGID) associates a physical saved segment to its logical saved segments. See *logical* *saved* *segment* and *saved* *segment*.

physical screen

**physical** **screen**. Synonym for *screen*.

preferred virtual machine

**preferred** **virtual** **machine**. A particular virtual machine that has one or more of the performance options assigned to it.

prepared

**prepared**. In SFS, a synonym for *in-doubt*.

preventive service

**preventive** **service**. The massive application of PTFs from the PUT. Contrast with *selective* *preventive* *service*.

primary system operator

**primary** **system** **operator**. The first CP privilege class A user logged on to VM/ESA after system initialization.

private resource

**private** **resource**. A resource accessible from anywhere within a TSAF collection or SNA network and whose identity is known only within a single virtual machine. Contrast with *global* *resource* and *local* *resource*.

private resource manager

**private** **resource** **manager**. An application that runs in a server virtual machine and provides a service for connecting programs, but that does not identify itself to the TSAF collection. Contrast with *global* *resource* *manager* and *local* *resource* *manager*.

privilege class

**privilege** **class**. One or more classes assigned to a virtual machine user in a VM/ESA directory entry; each privilege class specified lets a user access a logical subset of the CP commands. There are nine IBM-defined privilege classes that correspond to specific administrative functions. They are: Class A - primary system operator Class B - system resource operator Class C - system programmer Class D - spooling operator Class E - system analyst Class F - service representative Class G - general user Class H - reserved for IBM use Class Any - available to any user. The privilege classes can be changed to meet the needs of an installation. See *class* *authority* and *user* *class* *restructure* *(UCR)*.

privileged instruction simulation

**privileged** **instruction** **simulation**. The CP-incurred overhead to handle privileged instructions for virtual machine operating systems that execute as if they were in supervisor state but which are executing in problem state under VM/ESA. See *virtual* *machine* *assist* *(VMA)*.

problem state

**problem** **state**. A state during which the central processing unit cannot execute I/O and other privileged instructions. VM/ESA runs all virtual machines in problem state. See *privileged* *instruction* *simulation*. Contrast with *supervisor* *state*.

Procedures Language VM/REXX

**Procedures** **Language** **VM/REXX**. A component of VM/ESA. It contains the Procedures Language VM/REXX Interpreter, which processes the REXX language. It also contains the VM implementation of the SAA Procedures Language.

process

**process**. A systematic sequence of operations to produce a specified result. A process is usually logical, not physical.

product

**product**. Any separately installable software program, whether supplied by IBM or otherwise, distinct from others and recognizable by a unique identification code. The product identification code is unique to a given product, but does not identify the release level of that product.

programmed function (PF) key

**programmed** **function** **(PF)** **key**. On a terminal, a key that can do various functions selected by the user or determined by an application program.

program state

**program** **state**. A state associated with each program (source and target) in a conversation. This state defines the functions that a communication program can issue at a given time.

program status word (PSW)

**program** **status** **word** **(PSW)**. An area in storage that indicates the order in which instructions are executed, and to hold and indicate the status of the computer system.

program update service

**program** **update** **service**. Receiving the contents of a PUT, applying all or some of the changes, and rebuilding the serviced parts. See *preventive* *service* and *selective* *preventive* *service*.

program update tape (PUT)

**program** **update** **tape** **(PUT)**. A tape containing a customized collection of service tapes (preventive service) to match the products listed in a customer's ISD (IBM Software Distribution) profile. Each PUT contains cumulative service for the customer's products back to earlier release levels of the product still supported. The tape is distributed to authorized customers of the products at scheduled intervals or on request.

prompt

**prompt**. A displayed message that describes required input or gives operational information.

protected conversation

**protected** **conversation**. An APPC conversation that is allocated (initiated) with the SYNC_LEVEL=SYNCPT option between two application programs. When one of the application programs issues a commit (or roll back), the CRR sync point manager notifies (by means of the protected conversation) the other application program to issue a commit (or roll back). CRR processing handles the actual committing (or rolling back) of both of the application programs' work. Applications that use protected conversations must follow the rules of the LU 6.2 sync point architecture.

protocol

**protocol**. A set of rules for communication that are mutually understood and followed by two communicating stations or processes. The protocol specifies actions that can be taken by a station when it receives a transmission or detects an error condition.

pseudo timer

**pseudo** **timer**. A special VM/ESA timing facility that provides date, time, virtual processor, and total processor time information to a virtual machine.

PSS

**PSS**. Program support services.

PSW

**PSW**. Program status word.

PUT

**PUT**. Program update tape..

<a id="GLS"></a>

R

read-only access

**read-only** **access**. An access mode associated with a virtual disk or SFS directory that lets a user read, but not write or update, any file on the disk or SFS directory.

read/write access

**read/write** **access**. An access mode associated with a virtual disk or SFS directory that lets a user read and write any file on the disk or SFS directory (if write authorized).

real address

**real** **address**. The address of a location in real storage or the address of a real I/O device.

real machine

**real** **machine**. The actual processor, channels, storage, and I/O devices required for VM/ESA operation.

receive

**receive**. (1) Bringing into the specified buffer data sent to the user's virtual machine from another virtual machine or from the user's own virtual machine. (2) To load service files from a service tape.

register

**register**. See *general* *register*.

remote

**remote**. Two entities (for example, a user and a server) are said to be remote to each other if they belong to different systems within a collection, or to different nodes within an SNA network. Contrast with *local*.

Remote Spooling Communications Subsystem Networking (RSCS)

**Remote** **Spooling** **Communications** **Subsystem** **Networking** **(RSCS)**. An IBM licensed program and special-purpose subsystem that supports the reception and transmission of messages, files, commands, and jobs over a computer network.

reply

**reply**. (1) A response to an inquiry. (2) In SNA, a request unit sent only in reaction to a received request unit.

requester

**requester**. (1) The name given to a virtual machine containing a user program that requests a resource. (2) The program that relays a request to another computer through the

Contrast with server.

reserved file types

**reserved** **file** **types**. (1) File types recognized by the CMS editors (EDIT and XEDIT) as having specific default attributes that include: record size, tab settings, truncation column, and uppercase or lowercase characters associated with that particular file type. The CMS Editor creates a file according to these attributes. (2) File types recognized by CMS commands; that is, commands that only search for and use particular file types or create one or more files with a particular file type.

resource

**resource**. A program, a data file, a specific set of files, a device, or any other entity or a set of entities that the user can uniquely identify for application program processing in a VM system.

resource ID

**resource** **ID**. A one-to-eight character name that identifies a resource.

resource manager

**resource** **manager**. An application running in a server virtual machine that directly controls one or more VM resources. There are three categories of VM resource managers: global, local, and private. Also, a resource manager (such as the SFS file pool server), may participate in CRR.

Restructured Extended Executor (REXX) language

**Restructured** **Extended** **Executor** **(REXX)** **language**. A general-purpose programming language, particularly suitable for EXEC procedures, XEDIT macros, or programs for personal computing. Procedures, XEDIT macros, and programs written in this language can be interpreted by the Procedures Language VM/REXX Interpreter. Contrast with *CMS* *EXEC* *language* and *EXEC* *2* *language*.

resynchronization

**resynchronization**. CRR function that is performed by the CRR recovery server when there has been a failure during sync point processing for a transaction. Resynchronization, which involves exchanging log names and comparing logical unit of work states, automatically attempts to complete the sync point process for the transaction. The goal of resynchronization is to maintain a consistent state (data integrity) among the protected resources involved in a transaction. Resynchronization may complete after the application ends. In very rare cases, such as an irrecoverable media failure or an operator error, resynchronization cannot complete and CRR lets operator intervention complete the transaction.

rollback

**rollback**. (1) In the context of SFS, undoing changes that were made to a resource (such as a file). (2) In the context of CRR, the action taken by CRR for an application program (or transaction program) to initiate CRR backout processing to undo updates to protected resources (such as SFS file pools) during a transaction. See *backout*. The verb form of rollback is roll back.

rotational position sensing (RPS)

**rotational** **position** **sensing** **(RPS)**. A standard or optional feature of most IBM disk storage devices. It lets these devices disconnect from a block-multiplexer channel (or its equivalent on Model 3115/3125 processing units) during rotational positioning operations, thereby letting the channel service other devices.

route

**route**. A connection to another system by a logical link and one or more intermediate systems. In TSAF, many links and possible intermediate systems that allow the connection of one system to another.

RPS

**RPS**. Rotational position sensing.

RSCS

**RSCS**. Remote Spooling Communications Subsystem Networking.

run list

**run** **list**. A queue of virtual machines that are executable and currently competing for processor resources. Virtual machines take turns being dispatched for short periods of time (time slices) until they either complete a queue slice or go into a long WAIT state. Virtual machines in the run list can be briefly nonrunnable--for instance, waiting for a page swap-- without being dropped from the run list. The virtual machines in the run list are sorted by deadline priority. See *eligible* *list*..

<a id="GLS"></a>

S

saved segment

**saved** **segment**. A segment of storage that has been saved and assigned a name. The saved segment(s) can be physical saved segment(s) that CP recognizes or logical saved segments that CMS recognizes. The segments can be loaded and shared among virtual machines, which helps use real storage more efficiently, or a private, nonshared copy can be loaded into a virtual machine. See *logical* *saved* *segment* and *physical* *saved* *segment*.

saved system

**saved** **system**. A special nonrelocatable copy of a virtual machine's virtual storage and associated registers kept on a CP-owned disk and loaded by name instead of by I/O device address. Loading a saved system by name substantially reduces the time it takes to IPL the system in a virtual machine. Also, a saved system such as CMS can also share one or more 1MB segments of reenterable code in real storage between virtual machines. This reduces the cumulative real main storage requirements and paging demands of such virtual machines.

scale

**scale**. A line on the System Product Editor's (XEDIT) full-screen display, used for column reference.

SCIF

**SCIF**. Single console image facility.

SCP

**SCP**. System control programming.

screen

**screen**. An illuminated display surface; for example, the display surface of a CRT. Synonymous with *physical* *screen*.

SCRIPT/VS

**SCRIPT/VS**. A component of the IBM Document Composition Facility program product available from IBM for a license fee.

secondary user

**secondary** **user**. When a user is disconnected -- that is, has no virtual console on line--a secondary user can be designated to receive the disconnected user's console messages and to enter commands to the disconnected user's console.

second-level storage

**second-level** **storage**. The storage that appears real to a virtual machine. Contrast with *first-level* *storage* and *third-level* *storage*.

segment

**segment**. In System/370 architecture, 64KB of virtual storage. In 370-XA, ESA/370 and ESA/390 architecture, 1MB of virtual storage.

segment space

**segment** **space**. A saved segment is composed of up to 64 member saved segments accessed by a single name. A segment space occupies one or more architecturally-defined segments. In VM/ESA (ESA Feature), an architecturally-defined segment begins and ends on a 1MB boundary. In VM/ESA (370 Feature), an architecturally-defined segment begins and ends on a 64K boundary. A user with access to a segment space has access to all of its members. See *discontiguous* *saved* *segment*, *member* *saved* *segment*, *saved* *segment*, and *segment*.

selective preventive service

**selective** **preventive** **service**. The selective application of PTFs from the PUT. Contrast with *preventive* *service*.

separator

**separator**. Synonym for *delimiter*.

server

**server**. (1) The general name for a virtual machine that provides a service for a requesting virtual machine. (2) The program that responds to a request from another computer or the same computer through

<a id="PI"></a>

Contrast with *requester*.

server-requester programming interface (SRPI)

**server-requester** **programming** **interface** **(SRPI)**. (1) A protocol between requesters and servers in an enhanced connectivity network. Includes the protocol to define a cooperative processing subsystem. (2) The interface that enables enhanced connectivity between requesters and servers in a network.

service

**service**. Changing a product after installation. See *corrective* *service,* *local* *service,* and *program* *update* *service*.

service machine

**service** **machine**. A virtual machine running a program that provides system-wide services.

service virtual machine

**service** **virtual** **machine**. A virtual machine that provides a system service such as accounting, error recording, monitoring, or that provided by a supported licensed program.

session

**session**. The SNA term for a connection between two LUs. The LUs involved allocate conversations across sessions.

sever

**sever**. Ending communication with another virtual machine or with the user's own virtual machine.

SFS

**SFS**. Shared file system.

SFS directory

**SFS** **directory**. A group of files. SFS directories can be arranged to form a hierarchy in which one directory can contain one or more subdirectories as well as files.

shared file system (SFS)

**shared** **file** **system** **(SFS)**. A part of CMS that lets users organize their files into groups known as *directories* and selectively share those files and directories with other users.

shared segment

**shared** **segment**. A feature of a saved system or physical saved segment that lets one or more segments of reentrant code or data in real storage be shared among many virtual machines. For example, if a saved CMS system was generated, the CMS nucleus is shared in real storage among all CMS virtual machines loaded by name; that is, every CMS machine's segment of virtual storage maps to the same 64K of real storage. See *discontiguous* *saved* *segment* and *saved* *system*.

signaling attention

**signaling** **attention**. An indication that a user has pressed a key or entered a CP command to present an attention interrupt to CP or to the user's virtual machine.

simultaneous peripheral operations online (SPOOL)

**simultaneous** **peripheral** **operations** **online** **(SPOOL)**. (1) (Noun) An area of auxiliary storage defined to temporarily hold data during its transfer between peripheral equipment and the processor. (2) (Verb) To use auxiliary storage as a buffer storage to reduce processing delays when transferring data between peripheral equipment and the processing storage of a computer.

single console image facility (SCIF)

**single** **console** **image** **facility** **(SCIF)**. (1) Lets a user, who is disconnected from a primary virtual console, continue to have console communications by way of the console of the secondary user. See *secondary* *user*. (2) Enables a virtual machine operator to control multiple virtual machines from one physical terminal.

sink virtual machine

**sink** **virtual** **machine**. In VMCF, the virtual machine that receives messages or data from a source virtual machine. Contrast with *source* *virtual* *machine*.

SIO

**SIO**. Start I/O.

SMSG function

**SMSG** **function**. A CP function that lets a virtual machine send a special message to another virtual machine programmed to accept and process the message. See *special* *message*.

SNA

**SNA**. Systems Network Architecture.

source virtual machine

**source** **virtual** **machine**. In VMCF, the virtual machine that initiates the sending of messages or data to another virtual machine. Contrast with *sink* *virtual* *machine*.

special message

**special** **message**. A data transmission, made up of instructions or commands, sent from one virtual machine to another by means of the SMSG function. A special message is processed by the receiving virtual machine and does not appear on the receiver's console. See *SMSG* *function*.

SPOOL

**SPOOL**. Simultaneous peripheral operations online.

spool file

**spool** **file**. A collection of data along with CCWs for processing on a unit record device. Contrast with *system* *data* *file*.

spool file block

**spool** **file** **block**. A 4096-byte buffer that contains control information, in addition to records. Synonymous with *spool* *file* *buffer* *linkage* *block*.

spool file buffer linkage block

**spool** **file** **buffer** **linkage** **block**. Synonym for *spool* *file* *block*.

spool file class

**spool** **file** **class**. A one-character class associated with each virtual unit record device. For input spool files, the spool file class lets the user control which input spool files are read next; and, for output spool files, it lets the spooling operator better control or reorder the printing or punching of spool files having similar characteristics or priorities. The spool file class value can be A through Z or 0 through 9.

spool ID

**spool** **ID**. A spool file identification number automatically assigned by CP when the file is closed. The spool ID number can be from 0001 to 9900; it is unique for each spool file. To identify a given spool file, a user must specify the owner's user ID, the virtual device type, and the spool ID.

spooling

**spooling**. The processing of files created by or intended for virtual readers, punches, and printers. The spool files can be sent from one virtual device to another, from one virtual machine to another, and to real devices. See *virtual* *console* *spooling*.

<a id="GLS"></a>

SR

SR. Symptom record.

<a id="GLS"></a>

<a id="PI"></a>

Server-requester programming interface.

storage key

**storage** **key**. An indicator associated with one or more storage blocks that requires that tasks have a matching protection key to use the blocks.

string

**string**. A group of minidisks defined for a specific function in the product parameter file, for example, the BASE2 string, which holds source code.

subcommand

**subcommand**. The commands of processors such as EDIT or System Product Editor (XEDIT) that run under CMS.

supervisor call instruction (SVC)

**supervisor** **call** **instruction** **(SVC)**. An instruction that interrupts a program being executed and passes control to the supervisor so that it can do a specific service indicated by the instruction.

supervisor state

**supervisor** **state**. A state during which the processor can execute I/O and **othe**r privileged instructions. Only CP can execute in the supervisor state; all virtual machine operating systems run in problem state. Contrast with *problem* *state*.

SVC

**SVC**. Supervisor call instruction.

synchronization point processing

**synchronization** **point** **processing**. Consists of the SPM driving the participating resource adapters through the following SPM exits: Pre-coordination - checks participating resources to ensure they are ready for a sync point. Coordination - is the actual sync point, which implements the one-phase and two-phase commit protocols. Post-coordination - performs cleanup processing after a sync point. There are also the following exits, but they are not considered sync point exits: End of work unit - does cleanup processing before the work unit ends. Backout required - puts the protected resource in a state such that rollback (backout) is required.

sync point manager

**sync** **point** **manager**. Synchronization point manager.

sync point

**sync** **point**. See *synchronization* *point* *processing*.

syntax

**syntax**. The rules for the construction of a command or program.

system administrator

**system** **administrator**. The person responsible for maintaining a computer system.

system control programming (SCP)

**system** **control** **programming** **(SCP)**. IBM-supplied programming fundamental to the operation and maintenance of the system. It serves as an interface with IBM licensed programs and user programs and available without additional charge.

system data file

**system** **data** **file**. In the ESA Feature of VM, a collection of data associated with a particular function. Types of system data files include saved segments, NSSs, UCR files, image libraries, message repository files, and system trace files. Because a system data file contains no CCWs, it cannot be processed on a unit record device. Contrast with *spool* *file*.

System/370 mode

**System/370** **mode**. A virtual machine operating mode in which System/370 functions are simulated. Contrast with *ESA/370,* *ESA/390* *mode,* and *370-XA* *mode*.

system resource operator privilege class

**system** **resource** **operator** **privilege** **class**. The CP privilege class B user, who controls all the real resources of the machine, such as real storage, disk drives, and tape drives, not controlled by the primary system or spooling operators.

system trace file

**system** **trace** **file**. A type of system data file that contains CP or virtual machine trace data.

Systems Network Architecture (SNA)

**Systems** **Network** **Architecture** **(SNA)**. The description of the logical structure, formats, protocols, and operational sequences for transmitting information units through and controlling the configuration and operation of networks..

<a id="GLS"></a>

T

target

**target**. One of many ways to identify a line to be searched for by the System Product Editor (XEDIT). A target can be specified as an absolute line number, a relative displacement from the current line, a line name, or a string expression.

task

**task**. A basic unit of work used for the execution of a program or a system function.

T-disk

**T-disk**. Synonym for *temporary* *disk*.

temporary disk

**temporary** **disk**. An area on a DASD available to the user for newly created or stored files until logoff, at which time the area is released. Temporary disk space is allocated to the user during logon or when entering the CP DEFINE command. Synonymous with *T-disk*.

terminal

**terminal**. A device, usually equipped with a keyboard and a display, capable of sending and receiving information.

third-level storage

**third-level** **storage**. The virtual storage created and controlled by an OS/VS or VM virtual machine. Contrast with *first-level* *storage* and *second-level* *storage*.

time-of-day (TOD) clock

**time-of-day** **(TOD)** **clock**. A hardware feature required by VM/ESA. The TOD clock is incremented once every microsecond, and provides a consistent measure of elapsed time suitable for the indication of date and time; it runs regardless of the processor state (running, wait, or stopped).

time stamp

**time** **stamp**. A record containing the TOD clock value stored in its internal 32-bit binary format.

TOD clock

**TOD** **clock**. Time-of-day clock.

tokenized PLIST (parameter list)

**tokenized** **PLIST** **(parameter** **list)**. A string of doubleword aligned parameters occupying successive doublewords.

TPN

**TPN**. Transaction program name.

trace table

**trace** **table**. Synonym for *CP* *trace* *table*.

transaction

**transaction**. See *logical* *unit* *of* *work* (in terms of CRR) or *LUWID*.

transaction program

**transaction** **program**. (1) An application that runs within a particular LU. Within an SNA-defined network, a resource in a VM system or TSAF collection is viewed as a transaction program, within the LU that represents the VM system or TSAF collection. (2) In the context of CRR, an application program that executes one or more transactions or CRR logical units of work.

transaction program name (TPN)

**transaction** **program** **name** **(TPN)**. A symbolic name given to a particular transaction program in an SNA-defined network.

translate mode

**translate** **mode**. The operating mode of a virtual machine when virtual addresses are converted to real addresses by segment and page tables.

Transparent Services Access Facility (TSAF)

**Transparent** **Services** **Access** **Facility** **(TSAF)**. A component of VM/ESA that handles communication between systems by letting APPC/VM paths span multiple VM systems. TSAF lets a source program connect to a target program by specifying a name that the target has made known, instead of specifying a user ID and node ID.

truncation

**truncation**. A valid shortened form of CP, CMS, GCS, IPCS, RSCS, TSAF (Query only) command names, operands, and options that can be entered. When the shortened form is used, the number of key strokes is reduced. For example, the ACCESS command has a minimum allowable truncation of two, so AC, ACC, ACCE, ACCES, and ACCESS are all recognized by CMS as the ACCESS command. Contrast with *command* *abbreviation*.

TSAF

**TSAF**. Transparent Services Access Facility.

TSAF collection

**TSAF** **collection**. A group of VM processors, each with a TSAF virtual machine, connected by CTC, binary synchronous lines, or LANs.

TSAF virtual machine

**TSAF** **virtual** **machine**. The virtual machine that lets user programs connect to and communicate with virtual machines on different VM systems..

<a id="GLS"></a>

U

UCR

**UCR**. User class restructure.

UCS

**UCS**. Universal character set.

unit record device

**unit** **record** **device**. A reader, a printer, or a punch.

universal character set (UCS)

**universal** **character** **set** **(UCS)**. A printer feature that permits a variety of character arrays. Synonym for *printer* *universal* *character* *set*.

user

**user**. Anyone who requests the services of a computing system.

user class

**user** **class**. A privilege category assigned to a virtual machine user in the user's directory entry; each class specified allows access to a logical subset of all the CP commands. See *privilege* *class*.

user class restructure (UCR)

**user** **class** **restructure** **(UCR)**. The extension of the class structure of CP instructions from 8 to 32 classes for each user, command, and diagnose code within the system. This extension allows the installation greater flexibility in authorizing CP instructions.

user data

**user** **data**. In a file pool, any data that resides in storage groups 2 through 32767.

user ID

**user** **ID**. User identification.

user modification

**user** **modification**. Any change that a user originates for a product or component.

user program

**user** **program**. A transaction program that requests a service from a resource manager program. User programs reside in requester virtual machines.

user-written CMS command

**user-written** **CMS** **command**. Any CMS file created by a user that has a file type of MODULE or EXEC. Such a file can be executed as if it were a CMS command by issuing its file name, followed by any operands or options expected by the program or EXEC procedure..

<a id="GLS"></a>

V

Vector Facility (VF)

**Vector** **Facility** **(VF)**. A hardware feature that provides synchronous instruction processing for high-speed manipulation of fixed-point and floating-point data.

V=F machine

**V=F** **machine**. Virtual=Fixed machine.

virtual address

**virtual** **address**. The address of a location in virtual storage. A virtual address must be translated into a real address to process the data in processor storage.

virtual console

**virtual** **console**. A console simulated by CP on a terminal such as a 3270. The virtual device type and I/O address are defined in the VM/ESA directory entry for that virtual machine.

virtual console function

**virtual** **console** **function**. A CP command that the Diagnose Interface executes.

virtual console spooling

**virtual** **console** **spooling**. The writing of console I/O on disk as a printer spool file instead of, or in addition to, having it typed or displayed at the virtual machine console. The console data includes messages, responses, commands, and data from or to CP and the virtual machine operating system. The user can invoke or terminate console spooling at anytime. When the console spool file is closed, it becomes a printer spool file. Synonymous with *console* *spooling*.

virtual CPU time

**virtual** **CPU** **time**. The time required to execute the instructions of the virtual machine.

virtual disk

**virtual** **disk**. A logical subdivision (or all) of a physical disk storage device that has its own address, consecutive storage space for data, and an index or description of the stored data so that the data can be accessed. A virtual disk is also called a minidisk. See *disk*.

virtual=fixed machine (V=F machine)

**virtual=fixed** **machine** **(V=F** **machine)**. In the ESA Feature of VM, a preferred virtual machine with a fixed, contiguous area of host real storage that does not start at page 0. CP provides performance enhancements for this virtual machine. See *multiple* *preferred* *guests* *preferred* *virtual* *machine*, *virtual=real* *area* *(V=R* *area)*, *virtual=real* *machine*, and *virtual=virtual* *machine*.

virtual interval timer assist

**virtual** **interval** **timer** **assist**. A hardware assist function, available only on a processor, that has ECPS. It provides, if desired, a hardware updating of each virtual machine's interval timer at location X'50'.

virtual machine (VM)

**virtual** **machine** **(VM)**. In VM, a functional equivalent of a computing system. On the 370 Feature of VM, a virtual machine operates in System/370 mode. On the ESA Feature of VM, a virtual machine operates in System/370, 370-XA, ESA/370, or ESA/390 mode. Each virtual machine is controlled by an operating system. VM controls the concurrent execution of multiple virtual machines on an actual processor complex.

virtual machine assist (VMA)

**virtual** **machine** **assist** **(VMA)**. A hardware feature available on certain VM/ESA-supported System/370 models, that causes a significant reduction in the real supervisor state time that VM/ESA uses to control the operation of virtual storage systems such as VSE, DOS/VS and OS/VS and, to a lesser extent, CMS, DOS, and OS when running under VM/ESA. VM/ESA supervisor state time is reduced because the VMA feature, instead of VM/ESA, intercepts and handles interruptions caused by SVCs, other than SVC 76, and certain privileged instructions. See *CP* *assist,* *expanded* *virtual* *machine* *assist,* *Extended* *Control* *Program* *Support* *(ECPS:**VM/370),* and *virtual* *interval* *timer* *assist*.

virtual machine communication facility (VMCF)

**virtual** **machine** **communication** **facility** **(VMCF)**. A CP function that provides a method of communication and data transfer between virtual machines operating under the same VM/ESA system.

virtual machine group

**virtual** **machine** **group**. The concept in GCS of two or more virtual machines associated with each other through the same named system (for example, IPL GCS1). Virtual machines in a group share common read/write storage and can communicate with one another through facilities provided by GCS. Synonymous with *group*.

Virtual Machine/Enterprise Systems Architecture (VM/ESA)

**Virtual** **Machine/Enterprise** **Systems** **Architecture** **(VM/ESA)**. An IBM licensed program that manages the resources of a single computer so that multiple computing systems appear to exist. Each virtual machine is the functional equivalent of a *real* machine.

virtual printer (or punch)

**virtual** **printer** **(or** **punch)**. A printer (or card punch) simulated on disk by CP for a virtual machine. The virtual device type and I/O address are usually defined in the VM/ESA directory entry for that virtual machine.

virtual=real area (V=R area)

**virtual=real** **area** **(V=R** **area)**. The part of real storage, starting with real page 1, where a virtual=real machine can execute. CP maintains control of real page zero; only page zero of the virtual=real machine is relocated. Only one virtual machine at a time can occupy the virtual=real area. The area must be defined during VM/ESA system generation to contain the largest virtual=real machine likely to run. See *virtual=real* *option*.

virtual=real machine (V=R machine)

**virtual=real** **machine** **(V=R** **machine)**. A preferred virtual machine with a fixed, contiguous area of host real storage that starts at page 0. In the ESA Feature of VM, CP provides performance enhancements and an automatic recovery facility for this virtual machine. See *multiple* *preferred* *guests*, *preferred* *virtual* *machine*, *virtual=real* *area*, *virtual=real* *machine* *recovery*, and *virtual=virtual* *machine*.

virtual=real machine recovery (V=R machine recovery)

**virtual=real** **machine** **recovery** **(V=R** **machine** **recovery)**. On the ESA Feature of VM, a CP function that lets the V=R machine resume operation after most CP abnormal terminations. When possible, the facility reestablishes the V=R machine environment, allowing the operating system running in that virtual machine to perform its own recovery processes. See *automatic* *software* *re-IPL*.

virtual=real option

**virtual=real** **option**. A VM/ESA performance option that lets a virtual machine run in VM/ESA's virtual=real area. This option eliminates CP paging and optionally, CCW translation for this virtual machine. Synonymous with *V=R*.

virtual storage

**virtual** **storage**. Storage space that can be regarded as addressable main storage by the user of a computer system in which virtual addresses are mapped into real addresses. The size of virtual storage is limited by the addressing scheme of the computing system and by the amount of auxiliary

available, and not by the actual number of main storage locations.

virtual storage extended (VSE)

**virtual** **storage** **extended** **(VSE)**. The generalized term that indicates the combination of the DOS/VSE system control program and the VSE/Advanced Functions licensed program. Note that in certain cases, the term DOS is still used as a generic term; for example, disk packs initialized for use with VSE or any predecessor DOS or DOS/VS system are sometimes called DOS disks. Also note that the DOS-like simulation environment provided under the VM/ESA CMS component and CMS/DOS exists on VM/ESA licensed programs and continues to be called CMS/DOS.

Virtual Telecommunications Access Method (VTAM)

**Virtual** **Telecommunications** **Access** **Method** **(VTAM)**. An IBM licensed program that controls communication and the flow of data in a computer network. It provides single-domain, multiple-domain, and multiple-network capability. VTAM runs under MVS, OS/VS1, VM/ESA, and VSE.

virtual=virtual machine (V=V machine)

**virtual=virtual** **machine** **(V=V** **machine)**. A virtual machine that runs in the dynamic paging area. CP pages this virtual machine's guest real storage in and out of host real storage. See *dynamic* *paging* *area*, *virtual=fixed* *machine*, and *virtual=real* *machine*.

VM

**VM**. Virtual machine.

VMA

**VMA**. Virtual machine assist.

VMCF

**VMCF**. Virtual machine communication facility.

VM/ESA

**VM/ESA**. Virtual Machine/Enterprise Systems Architecture.

VM/Pass-Through Facility

**VM/Pass-Through** **Facility**. A facility that lets VM users interactively access remote system and processor nodes. These can be remote IBM 4300 processors, other VM systems, with or without this facility installed, or System/370-compatible non-VM systems.

VM directory

**VM** **directory**. A CP disk file that defines each virtual machine's typical configuration; the user ID, password, regular and maximum allowable virtual storage, CP command privilege class or classes allowed, dispatching priority, logical editing symbols to be used, account number, and CP options desired. Synonymous with *CP* *directory*.

V=R

**V=R**. Synonym for *virtual=real* *option*.

V=R area

**V=R** **area**. Virtual=real area.

VSE

**VSE**. Virtual storage extended.

VTAM

**VTAM**. Virtual Telecommunications Access Method..

<a id="GLS"></a>

W

warm start

**warm** **start**. (1) The result of an IPL that does not erase previous system data. (2) The automatic reinitialization of the VM/ESA control program that occurs if the control program cannot continue processing. Closed spool files and the VM/ESA accounting information are not lost. Contrast with *checkpoint* *(CKPT)* *start,* *cold* *start*, and *force* *start*.

windowing

**windowing**. A set of functions that lets the user view and manipulate data in user-defined areas of the physical screen called *windows*. Windowing support lets the user define, position, and overlay windows; scroll backward and forward through data; and write data into virtual screens.

work unit

**work** **unit**. In CMS, a group of related operations that can be either committed or rolled back as a unit. When the operations associated with a work unit are committed or rolled back, new operations can be associated with the same work unit. These operations can also be committed or rolled back. (The work unit is, in a sense, reusable.) Multiple work units may be active. See *active* *work* *unit*, *inactive* *work* *unit*, and *logical* *unit* *of* *work*..

<a id="GLS"></a>

X

XA mode

**XA** **mode**. A GCS mode of operation on ESA that uses the full capabilities of the Extended Systems Architecture.

XEDIT

**XEDIT**. The CMS facility, containing the XEDIT command and XEDIT subcommands and macros, that lets a user create, change, and manipulate CMS files..

<a id="GLS"></a>

Z

zap

**zap**. To modify or dump an individual text file, using the ZAP command or the ZAPTEXT EXEC..

<a id="GLS"></a>

2

2305

**2305**. Refers to the IBM 2305 Fixed Head Storage Device, Models 1 and 2.

270X

**270X**. Refers to the IBM 2701, 2702, and 2703 Transmission Control Units or the Integrated Communications Adapter (ICA) on the System/370 Model 135.

2741

**2741**. Refers to the IBM 2741 Terminal. Information on the 2741 also applies to the IBM 3767 Terminal, unless otherwise noted..

<a id="GLS"></a>

3

370 mode

**370** **mode**. A GCS mode of operation on ESA that simulates 370 architecture. See *System/370* *mode*.

370-XA mode

**370-XA** **mode**. A virtual machine operating mode in which System/370-Extended Architecture functions are simulated. Contrast with *ESA/370* *mode,* *ESA/390* *mode,* and *System/370* *mode*.

3088

**3088**. Refers to the IBM 3088 Multisystem Communications Unit, Models 1 and 2.

3262

**3262**. Refers to the IBM 3262 Printer, Models 1 and 11.

3270

**3270**. Refers to a series of IBM display devices; for example, the IBM 3275, 3276 Controller Display Station, 3277, 3278, and 3279 Display Stations, the 3290 Information Panel, and the 3287 and 3286 printers. A specific device type is used only when a distinction is required between device types. Information about display terminal usage also refers to the IBM 3138, 3148, and 3158 Display Consoles when used in display mode, unless otherwise noted.

3284

**3284**. Refers to the IBM 3284 Printer. Information on the 3284 also pertains to the IBM 3286, 3287, 3288, and 3289 printers, unless otherwise noted.

3330

**3330**. Refers to the IBM 3330 Disk Storage Device.

3340

**3340**. Refers to the IBM 3340 Direct Access Storage Device.

3350

**3350**. Refers to the IBM 3350 Direct Access Storage Device when used in native mode.

3375

**3375**. Refers to the IBM 3375 Direct Access Storage Device.

3380

**3380**. Refers to the IBM 3380 Direct Access Storage Device.

3422

**3422**. Refers to the IBM 3422 Magnetic Tape Subsystem.

3480

**3480**. Refers to the IBM 3480 Magnetic Tape Subsystem.

370x

**370x**. Refers to the IBM 3704/3705 Communication Controllers.

3800

**3800**. Refers to the IBM 3800 Printing Subsystems. A specific device type is used only when a distinction is required between device types..

<a id="GLS"></a>

4

4245

**4245**. Refers to the IBM 4245 Printer.

4248

**4248**. Refers to the IBM 4248 Printer.

4250

**4250**. Refers to the IBM 4250 Printer..

<a id="GLS"></a>

9

9370

**9370**. Refers to a series of processors, namely the IBM 9373 Model 20, the IBM 9375 Models 40 and 60, the IBM 9377 Model 90, and other models..

<a id="GLS"></a>

---

[Previous](f-13-3.md) | [Index](README.md) | [Next](bibliography.md)
