[Previous](g-5.md) | [Index](README.md) | [Next](bibliography.md)

---

# GLOSSARY Glossary

<a id="HDRGLOSSY"></a>

Glossary

<a id="SPTGLOSS"></a>

<BOOK> <> <> <GC24-5518> <ANY> [<HCPB9>](#LNK) For a complete list of VM/ESA terms and their definitions, see the VM/ESA: [<BOOK> <> <> <GC24-5518>](#LNK)<ANY> <HCPB9> Master Index and Glossary. The list is also available in the online VM/ESA HELP Facility. For example, to display the definition of "cms", enter:

`help` `glossary` `cms` When you enter the HELP Facility's online glossary file, the definition of "cms" is displayed as the current line. Once you are in the glossary

file, you can simply search for the other terms. If you are unfamiliar with the HELP Facility, you can enter: he`lp` to display the main HELP Menu, or enter: `help` `cms` `help` for information about the HELP command. <BOOK> <> <> <SC24-5 [460> <ANY> <DMSB3>](#LNK) For more information on the HELP Facility, see the VM/ESA: CMS User's [<BOOK](#LNK)> <> <> <SC24-5460> <ANY> <DMSB3> <BOOK> <> <> <SC24- [5461> <ANY> <DMSB4>](#LNK) Guide. For more about the HELP command, see the VM/ESA: CMS Command [<BOOK> <>](#LNK)<> <SC24-5461> <ANY> <DMSB4> Reference. You can find additional information on IBM terminology in the Dictionary of Computing, SC20-1699., c.cp.

<a id="GLS"></a>

A

access mode

**access** **mode**. A method VM/ESA uses to control user access to data files. Access modes let the user read and write data to a file, or only read data from a file. See *file* *mode*.

alias

**alias**. A pointer to an SFS base file. An alias can be in the same directory as the base file or in a different directory. There must always be a base file for the alias to point to. The alias references the same data as the base file. Data is not moved or duplicated.

alphanumeric

**alphanumeric**. A character set that contains letters, digits, and usually other characters, such as punctuation marks.

APAR

**APAR**. Authorized program analysis report.

APAR number

**APAR** **number**. The number that IBM assigns to an APAR and to the change resulting from it.

application program

**application** **program**. A program written for or by a user that applies to the user's work, such as a program that does inventory control or payroll.

APPLIED

**APPLIED**. This status, listed in the apply status table, indicates a product or program temporary fix has been APPLIED to the system.

apply

**apply**. When servicing a product or component, to generate an auxiliary control structure from a PTF.

Apply disk

**Apply** **disk**. In VMSES/E, a minidisk or SFS directory containing the files that describe the maintenance levels: the apply status table, AUX files, version vector tables, the select data file, and the build status table.

apply list

**apply** **list**. A file listing PTFs applied to a product or component.

apply status table

apply status table. The Software Inventory table that identifies what

PTFs? have been applied to the product. The system level of the table identifies what product or component has been applied to the system. The file type of the system level inventory table is SYSAPPS and the file type of the service level inventory table is

<a id="VAPPS"></a>

Apply string

**Apply** **string**. In VMSES/E, the set of Apply disks.

area

**area**. A term acceptable for DASD space when there is no need to differentiate between space on count-key-data devices and FB-512 devices. See *DASD* *space*.

assembler language

**assembler** **language**. A source language that includes symbolic machine language statements in which there is a one-to-one correspondence with instruction formats and data formats of the computer.

authority

**authority**. In SFS, the permission to access a file or directory. You can have read authority or write authority (which includes read authority). You can also have file pool administration authority, which is the highest level of authority in a file pool.

authorized program analysis report (APAR)

**authorized** **program** **analysis** **report** **(APAR)**. An official request to the responsible IBM Change Team to look into a suspected problem with IBM code or documentation. APARs describe problems giving conditions of failure, error messages, abend codes, or other identifiers. They also contain a problem summary and resolution when applicable. See *program* *temporary* *fix* *(PTF)*.

AUX file

**AUX** **file**. Auxiliary control file.

auxiliary control file (AUX file)

**auxiliary** **control** **file** **(AUX** **file)**. A file that contains a list of file types of update files applied to a particular source file or to control the *service* level *used* during build. See control file and *preferred* *auxiliary* *file*. Synonymous with *auxiliary* *file*.

auxiliary file

**auxiliary** **file**. Synonym for *auxiliary* *control* *file*.

AVS

**AVS**. APPC/VM VTAM Support..

<a id="GLS"></a>

B

Base disk

**Base** **disk**. In VMSES/E, a minidisk or SFS directory containing the original product code.

base file

**base** **file**. The first occurrence of an SFS file. It remains the base for the life of the file, even if the file has been renamed. Aliases point to base files.

base file type

**base** **file** **type**. In VMSES/E, the file type used for a serviceable part when there is no service. The PTF number in the file type is set to "00000." For example, EXC00000 would be the base file type for an exec. See serviceable part.

Base string

**Base** **string**. IN VMSES/E, the set of Base disks.

block

**block**. (1) A unit of DASD space on FB-512 devices. For example, FB-512 devices can be the IBM 9335, 9332, 9313, 3370, and 3310 DASD using fixed-block architecture. (2) In CMS Multitasking, to stop the execution of a thread until a function has been completed or a condition is satisfied.

Bpi

**Bpi**. Bytes per inch.

bpi

**bpi**. Bits per inch.

build

**build**. In the installation and service of a product, to do the necessary steps to produce executable code or systems. This is often called the *build* *process*.

BUILDALL

**BUILDALL**. This status, shown in the service-level build status table, indicates the user requested that an object be built with the ALL option on the VMFBLD command, and the object still needs to be built.

Build disk

**Build** **disk**. In VMSES/E, a minidisk or SFS directory containing the running code for the product being serviced.

Build ID

Build ID. A 1- to 8- alphanumeric character identifier (bldid) that is

**used?** **to** name the Software Inventory files created during build processing. The user can change this value to define different maintenance levels.

build list

**build** **list**. An EXEC file that names the parts included in an object being built.

build requisites

**build** **requisites**. An object that is needed to build another object. For example, when one object is built using another object, the latter is a build requisite of the former. Also, if an object's build requisite is serviced, the object must be rebuilt after its build requisite is built.

build status table

build status table. The Software Inventory table that identifies what

products have been built, in the system level, and what individual objects have? been generated for the product, in the service level. The file type of the system level inventory table is SYSBLDS and the file type of the service level inventory table is

<a id="VBLDS"></a>

Build string

**Build** **string**. The set of Build disks.

build-time requisites

**build-time** **requisites**. Product(s) that must be installed before a certain product can run correctly.

BUILT

**BUILT**. This status, listed in the build status table, indicates that a product or object has been built on the system..

<a id="GLS"></a>

<a id="GLS"></a>

C

callable services library (CSL)

**callable** **services** **library** **(CSL)**. A package of CMS assembler routines that can be stored as an entity and made available to a high-level language, REXX, or an assembler program.

changes

**changes**. In installation and service, service supplied by IBM and original equipment manufacturers (OEMs) for their programs. In the IBM service process, there are many ways users can receive information they need to fix (change) a portion(s) of a product they are running on a VM system. These include PTFs, APARs, user modifications, and information received over the phone. All these types of information are called *changes*.

checkpoint (CKPT) start

**checkpoint** **(CKPT)** **start**. A VM/ESA system restart that attempts to recover information about closed spool files previously stored on the checkpoint cylinders. The spool file chains are reconstructed, but the original sequence of spool files is lost. Unlike warm start, CP accounting and system message information is also lost. Contrast with *cold* *start,* *force* *start,* and *warm* *start*.

circumventive service

**circumventive** **service**. Information that IBM supplies over the phone or on a tape to circumvent a problem by disabling a failing function until a PTF is available to be shipped as a corrective service fix. See *patch* and *zap*.

CKD

**CKD**. Count-key-data.

class A user

**class** **A** **user**. See *primary* *system* *operator* *privilege* *class.*

class authority

**class** **authority**. Privilege assigned to a virtual machine user in the user's directory entry; each class specified allows access to a subset of all the CP commands. See *privilege* *class* and *user* *class* *restructure* *(UCR)*.

CMS

**CMS**. Conversational Monitor System.

CMS EXEC

**CMS** **EXEC**. An EXEC procedure or EDIT macro written in the CMS EXEC language and processed by the CMS EXEC processor. Synonymous with *CMS* *program*.

CMS EXEC language

**CMS** **EXEC** **language**. A general-purpose, high-level programming language, particularly suitable for EXEC procedures and EDIT macros. The CMS EXEC processor executes procedures and macros (programs) written in this language. Contrast *with* EXEC *2* *language* and *Restructured* *Extended* *Executor* *(REXX)* *language*.

CMS minidisk file directory

**CMS** **minidisk** **file** **directory**. A directory on each CMS disk that contains the name, format, size, and location of each of the CMS files on that disk. When a disk is accessed by the ACCESS command, its directory is read into virtual storage and identified with any letter from A through Z. Synonymous with *master* *file* *directory* *block* and *minidisk* *directory*.

CMS nucleus

**CMS** **nucleus**. The portion of CMS that is resident in the user's virtual storage whenever CMS is executing. Each CMS user receives a copy of the CMS nucleus when the user IPLs CMS. See *saved* *system* and *shared* *segment*.

CNTRL file

**CNTRL** **file**. Control file with file type CNTRL.

cold start

**cold** **start**. A VM/ESA system restart that ignores previous data areas and accounting information in main storage, and the contents of paging and spool files on CP-owned disks. Contrast with *checkpoint* *(CKPT)* *start,* *force* *start,* and *warm* *start*.

command

**command**. A request from a user at a terminal for the execution of a particular CP, CMS, GCS, TSAF, Dump Viewing Facility, or AVS function. A CMS command can also be the name of a CMS file with a file type of EXEC or MODULE. See *subcommand* and *user-written* *CMS* *command*.

command line

**command** **line**. The line at the bottom of display panels that lets a user enter commands or panel selections. It is prefixed by an arrow (====>).

commit

**commit**. (1) In the context of SFS, to change a resource (such as a file) permanently. (2) In the context of CRR, to make permanent changes to protected resources (such as SFS file pools) during a transaction (CRR logical unit of work). CRR commits changes made by an application program or transaction program.

COMMITTED

**COMMITTED**. This status, listed in the receive status table, indicates that a PTF has been committed for the product. This means that obsolete parts of the PTF may be discarded.

common storage

**common** **storage**. A shared segment of reentrant code that contains free storage space, the GCS supervisor, control blocks, and data that all members of a virtual machine group share.

compile

**compile**. To translate a program written in a high-level programming language into a machine language program.

component

**component**. A collection of objects that together form a separate functional unit. A product may contain many components. For example, CP, CMS, and TSAF are components of VM/ESA.

component override

**component** **override**. Synonym for *component* *parameter* *override*.

component override area

**component** **override** **area**. An area of the product parameter file or of a product parameter override file that contains one or more component parameter overrides. Synonymous with *override* *area*.

component parameter override

component parameter override. A component parameter, defined in a

component override? area, that updates or replaces a component parameter defined in a component area of the product parameter file. Synonymous with *component* *override* and *override*.

concurrently

**concurrently**. Concerning a mode of operation that includes doing work on two or more activities within a given (short) interval of time.

console

**console**. A device used for communications between the operator or maintenance engineer and the computer.

console spooling

**console** **spooling**. Synonym for *virtual* *console* *spooling*.

console stack

**console** **stack**. Refers collectively to the program stack and the terminal input buffer.

control file

**control** **file**. (1) In service, a file with file type CNTRL that contains records that identify the updates to be applied and the macro libraries, if any, needed to assemble that source program. (2) A CMS file that is interpreted and directs the flow of a certain process through specific steps. For example, the control file could contain installation steps, default addresses, and PTF prerequisite lists and many other necessary items.

control program

**control** **program**. A computer program that schedules and supervises the program execution in a computer system. See *Control* *Program* *(CP)*.

Control Program (CP)

**Control** **Program** **(CP)**. A component of VM/ESA that manages the resources of a single computer so multiple computing systems appear to exist. Each of these apparent systems, or virtual machines, is the functional equivalent of an IBM System/370, 370-XA, or ESA computer. Also, XC virtual machines provide functions beyond the ESA architecture. See also *virtual* *machine*.

control section (CSECT)

**control** **section** **(CSECT)**. The part of a program specified by the programmer to be a relocatable unit, all elements of which are loaded into adjoining main storage.

control statement

**control** **statement**. A statement that controls or affects program execution in a data processing system.

copy file

**copy** **file**. A file having file type COPY that contains nonexecutable real storage definitions that are referred to by macros and assemble files.

copy function

**copy** **function**. The function initiated by a PF key to copy the contents of a display screen onto an associated hardcopy printer. A remote display terminal copies the entire contents of the screen onto a printer attached to the same control unit. A local display terminal copies all information from the screen, except the screen status information, onto any printer attached to any local display control unit.

COR

**COR**. Corrective service tape.

corequisite

**corequisite**. Corequisites identify other PTFs that must be applied at the same time this PTF is applied. No specific order is required for applying corequisite PTFs.

corequisite change

**corequisite** **change**. A change that must be applied to the user's product along with another change. For example, if the user needs to apply change1 to the system and change1 has a corequisite of change2, then the user must apply both change1 and change2 to the system, but not in a specific order. A corequisite change corrects a problem that requires changes to one or more elements of a product or component.

corrective service

**corrective** **service**. Service that IBM supplies on tape to correct a specific problem.

corrective service tape

**corrective** **service** **tape**. A tape, supplied by IBM at the user's request, containing a fix for a specific problem and any requisites for the fix.

count-key-data (CKD) device

**count-key-data** **(CKD)** **device**. A DASD that stores data in the format: count field, usually followed by a key field, followed by the actual data of a record. The count field contains the cylinder number, head number, record number, and the length of the data. The key field contains the record's key (search argument).

CP

**CP**. Control Program.

CP command

**CP** **command**. A command available to all VM users. Class G CP commands let the general user reconfigure their virtual machine, control devices attached to their virtual machine, do input and output spooling functions, and simulate many other functions of a real computer console. Other CP commands let system operators, system programmers, system analysts, and service representatives manage the resources of the system.

CP directory

**CP** **directory**. Synonym for *VM* *directory*.

CP read

**CP** **read**. The condition when CP is waiting for a response or request for work from the user. On a typewriter terminal, the keyboard is unlocked; on a display terminal, the screen status area indicates CP READ.

cross system extensions (CSE)

**cross** **system** **extensions** **(CSE)**. An environment in which end users attached to a single system can participate with additional systems as though all participating systems were one complex.

CSE

**CSE**. Cross system extensions.

CSECT

**CSECT**. Control section.

CSL

**CSL**. Callable services library.

cylinder

**cylinder**. In a disk pack, the set of all tracks with the same nominal distance from the axis about which the disk pack rotates..

<a id="GLS"></a>

<a id="GLS"></a>

D

DASD

**DASD**. Direct access storage device.

DASD Dump Restore (DDR) program

**DASD** **Dump** **Restore** **(DDR)** **program**. A service program that copies all or part of a minidisk onto tape, loads the contents of a tape onto a minidisk, or sends data from a DASD or from tape to the virtual printer.

DASD space

**DASD** **space**. (1) Area allocated to DASD units on CKD devices. (2) Area allocated to DASD units on FB-512 devices. Note that *DASD* *space* is synonymous with *cylinder* when there is no need to differentiate between CKD devices and FB-512 devices.

DBCS

**DBCS**. Double-byte character set.

DCSS

**DCSS**. Discontiguous saved segment.

DDR program

**DDR** **program**. DASD Dump Restore program.

DELETE

**DELETE**. This status, shown in the service-level build status table, indicates the object has been removed from the build list, and the corresponding object must be deleted.

DELETED

**DELETED**. This status, listed in the apply status table, indicates that a product has been deleted from the system. In the service-level build status table, it indicates that an object has been deleted from the product.

delimiter

**delimiter**. (1) A flag that separates and organizes items of data. Synonymous with *separator*. (2) A character that groups or separates words or values in a line of input. Usually one or more blank characters separate the command name and each operand or option in the command line. In certain cases, a tab, left parenthesis, or backspace character can also act as a delimiter.

Delta disk

**Delta** **disk**. In VMSES/E, a minidisk or SFS directory containing a list of the files on a PTF. See program temporary fix (PTF).

Delta string

**Delta** **string**. In VMSES/E, the set of Delta disks.

dependent PTF

**dependent** **PTF**. A PTF that has another PTF as a prerequisite or corequisite.

dependent requisite

dependent requisite. A dependent requisite is a product that must be

installed before another product can be installed correctly. Unlike pre-requisites, dependent requisites are no longer satisfied when the requisite product is superseded. This occurs when a product requires a specific? level of another product and newer levels of the product will not meet the requirements.

description table

description table. The Software Inventory table that contains the

**descriptive** **name?** for a product, in the system level, and APARs in the service level. The file type of the system level inventory table is SYSDESCT and the file type of the service level inventory table is

<a id="VDESCT"></a>

device support facilities

**device** **support** **facilities**. A program for doing operations on disk volumes so that they can be accessed by IBM and user programs. Examples of these operations are initializing a disk volume and assigning an alternate track.

DIRCONTROL directory

**DIRCONTROL** **directory**. Synonym for *directory* *control* *directory*.

direct access storage device (DASD)

**direct** **access** **storage** **device** **(DASD)**. A storage device in which the access time is effectively independent of the location of the data.

directory

**directory**. See *auxiliary* *directory,* *CMS* *minidisk* *file* *directory* *DIRCONTROL* *directory,* *directory* *control* *directory,* *file* *control* *directory,* *FILECONTROL* *directory,* *SFS* *directory,* *or* VM *directory*.

directory identifier (dirid)

**directory** **identifier** **(dirid)**. A fully-qualified directory name (in which the file pool ID and user ID can be allowed to default), a file mode letter, or plus (+) or minus (-) file mode syntax (used in commands).

directory name (dirname)

**directory** **name** **(dirname)**. A fully-qualified directory name that can incorporate a period (.) to indicate the user's own top directory (used in commands).

dirid

**dirid**. Directory identifier.

dirname

**dirname**. Directory name.

discontiguous saved segment

**discontiguous** **saved** **segment**. One or more segments of storage that were previously loaded, saved, and assigned a unique name. In VM/ESA, a segment begins and ends on a 1MB boundary. The segment can be shared among virtual machines if the segment contains reentrant code. Discontiguous segments used with CMS must be loaded into storage at locations above the address space of a user's CMS virtual machine. They can be detached when no longer needed.

disk

**disk**. A magnetic disk unit in the user's CMS virtual machine configuration. See *virtual* *disk*.

display device

**display** **device**. An I/O device that gives a visual representation of data.

display terminal

**display** **terminal**. A terminal with a component that can display information on a viewing surface such as a screen or gas panel.

distributed function terminal (DFT)

**distributed** **function** **terminal** **(DFT)**. An operational mode that allows multiple concurrent logical terminal sessions. Contrast with *control* *unit* *terminal* *(CUT)*.

DMSPARMS file

**DMSPARMS** **file**. A CMS file with a file type of DMSPARMS that contains the start-up parameters that SFS file pool server and CRR recovery server processing uses.

double-byte character set (DBCS)

**double-byte** **character** **set** **(DBCS)**. A character set that requires 2 bytes to uniquely define each character. This contrasts with EBCDIC, in which each printed character is represented by 1 byte.

dump

**dump**. To write the contents of part or all of main storage, or part or all of a minidisk, to auxiliary storage or a printer. See *abend* *dump*..

<a id="GLS"></a>

E

ECKD

**ECKD**. Extended count-key data.

edit

**edit**. A function that makes changes, additions, or deletions to a file on a disk. These changes are interactively made. The edit function also generates information in a file that did not previously exist.

ERROR

**ERROR**. This qualifier of the status field in the service-level build status table indicates that an error was encountered when building an object. In the system-level build status table, it indicates that an error was detected when building a product or object.

ESA virtual machine

**ESA** **virtual** **machine**. A virtual machine that simulates ESA/370 or ESA/390 functions. Contrast with *370* *virtual* *machine*, *XA* *virtual* *machine*, and *XC* *virtual* *machine*

exclude list

**exclude** **list**. A file listing PTFs to be omitted from a product or component.

EXEC procedure

EXEC procedure. (1) A procedure defined by a frequently used sequence of

CMS? and CP commands to do a commonly required function. A user creates the procedure to save repetitious reentering of the sequence, and invokes the entire procedure by entering a command (that is, the exec file's file name). The procedure could consist of a long sequence of CMS and CP commands, along with REXX, EXEC 2, or CMS EXEC control statements to control processing within the procedure. (2) A CMS file with a file type of EXEC.

EXEC 2 language

**EXEC** **2** **language**. A general-purpose, high-level programming language, particularly suitable for EXEC procedures and XEDIT macros. The EXEC 2 processor runs procedures and XEDIT macros (programs) written in this language. Contrast with *CMS* *EXEC* *language* and *Restructured* *Extended* *Executor* *(REXX)* *language*.

exit

**exit**. See *user* *exit* and *installation-wide* *exit*..

<a id="GLS"></a>

<a id="GLS"></a>

F

FB-512

**FB-512**. An FBA device that stores data in 512-byte blocks (refers to DASD devices such as the IBM 9335, 9332, 9313, 3370, and 3310).

FBA

**FBA**. Fixed-block architecture.

feature

**feature**. A feature is associated with the software distribution order number which has a type, model, and feature field. The feature field identifies a particular deliverable for the given product offering.

file access mode

**file** **access** **mode**. A file mode number that designates whether the file can be used as a read-only or read/write file by a user. See *file* *mode*.

file ID

**file** **ID**. A CMS file identifier that consists of a file name, file type, file mode, or directory ID. The file ID is associated with a particular file when the file is created, defined, or renamed under CMS. See *file* *name,* *file* *type,* and *file* *mode*.

file mode

**file** **mode**. A two-character CMS file identifier field containing the file mode letter (A through Z) followed by the file mode number (0 through 6). The file mode letter indicates the minidisk or SFS directory on which the file resides. The file mode number indicates the access mode of the file. See *file* *access* *mode*.

file name

**file** **name**. A one-to-eight character alphanumeric field, containing A through Z, 0 through 9, and special characters $ # @ + - (hyphen) (colon) _ (underscore), that is part of the CMS file identifier and serves to identify the file for the user.

file pool

**file** **pool**. A collection of minidisks managed by SFS. It contains user files and directories and associated control information. Many users' files and directories can be contained in a single file pool.

file type

**file** **type**. A one-to-eight character alphanumeric field, containing A through Z, 0 through 9, and special characters $ # @ + - (hyphen) (colon) _ (underscore), that is used as a descriptor or as a qualifier of the file name field in the CMS file identifier. See *reserved* *file* *types*.

file type abbreviation (ftabbrev)

**file** **type** **abbreviation** **(ftabbrev)**. The 3-character PTF abbreviation or the real CMS file type for a part that is not serviced by replacement.

file type abbreviation table

**file** **type** **abbreviation** **table**. The Software Inventory table that identifies the mapping between PTF-numbered file types and the real CMS file type. The service level inventory does not contain this table.

fixed-block architecture (FBA) device

**fixed-block** **architecture** **(FBA)** **device**. A disk storage device that stores data in blocks of fixed size or records; these blocks are addressed by block number relative to the beginning of the particular file.

flat file

**flat** **file**. A file that consists of a set of records ordered by record number or as sequentially entered in the file; a two dimensional file.

free storage

**free** **storage**. Storage not allocated. The blocks of central storage available for temporary use by programs or by the system.

ftabbrev

**ftabbrev**. File type abbreviation

full-pack minidisk

**full-pack** **minidisk**. A virtual disk that contains all of the addressable cylinders of a real DASD volume..

<a id="GLS"></a>

G

GCS

**GCS**. Group Control System for ESA/370 or ESA/390 architecture.

group

**group**. *Synonym* for *virtual* machine *group*.

Group Control System (GCS)

**Group** **Control** **System** **(GCS)**. A component of VM/ESA, consisting of a shared segment that the user can IPL and run in a virtual machine. It provides simulated MVS services and unique supervisor services to help support a native SNA network.

GROUP EXEC

**GROUP** **EXEC**. A GCS installation tool that prompts you for the specifications needed to build a GCS configuration file.

guest

**guest**. An operating system running in a virtual machine managed by a VM control program. Contrast with *host*..

<a id="GLS"></a>

H

hard requisites

**hard** **requisites**. The hard requisites of a PTF are a subset of its prerequisites. There are two reasons for a prerequisite to be classified as a hard requisite. First, if the PTF depends on a functional change introduced by the requisite, the requisite is considered a hard requisite. For example, the requisite introduces a new flag and the PTF exploits it. Second, if any of the updates in the PTF affect the same lines of code as the requisite, such that the new update will not apply without the older one, then it is a hard requisite relationship. (Corequisites and if-requisites are by definition hard requisites and are not explicitly listed as hard requisites).

history files

**history** **files**. One or more CMS files that describe the changes (with a date and time stamp) made to the VM/ESA system and its installed software products..

<a id="GLS"></a>

<a id="GLS"></a>

I

I/O

**I/O**. Input/output.

if-requisite

**if-requisite**. (1) At the system-level, an if-requisite lists two products. The first one becomes a requisite product if and only if the second one is installed. (2) At the service-level, an if-requisite lists a PTF in another product that must be applied if and only if the other product is installed.

image library

**image** **library**. A set of modules that define the spacing, characters, and copy modification data that a 3800 printer uses to print a spool file or that define the spacing and character set that an impact printer uses to print? a spool file. See system data file.

initial installation system

**initial** **installation** **system**. In VMSES/E, a functional subset of the VM/ESA system shipped on the VM/ESA system DDR tapes and used during installation of VM/ESA.

initial program load (IPL)

**initial** **program** **load** **(IPL)**. The initialization procedure that causes an operating system to begin operation. A VM user must IPL the specific operating system into the virtual machine that will control the user's work. Each virtual machine can be loaded with a different operating system.

initialize

**initialize**. To set counters, switches, addresses, or contents of storage to starting values.

input/output (I/O)

**input/output** **(I/O)**. (1) A device whose parts can do an input process and an output process at the same time. (2) A functional unit or channel involved in an input process, output process, or both, concurrently or not, and to the data involved in such a process.

installation-wide exit

**installation-wide** **exit**. An interface to VM/ESA that a system programmer can use to enhance or extend the functions of a VM/ESA system. Generally, an installation-wide exit is activated for all users on the system and is run as part of a system program.

install-time requisites

**install-time** **requisites**. Product(s) that must be installed before this product can be installed correctly.

interactive

**interactive**. The classification given to a virtual machine depending on this virtual machine's processing characteristics. When a virtual machine uses less than its allocation time slice because of terminal I/O, the virtual machine is classified as being interactive. Contrast with *noninteractive*.

interface

**interface**. A shared boundary between two or more entities. An interface might be a hardware or software component that links two devices or programs together.

interrupt

**interrupt**. A suspension of a process, such as execution of a computer program, caused by an external event and done in such a way that the process can be resumed.

invoke

**invoke**. To start a command, procedure, or program.

IPL

**IPL**. Initial program load..

<a id="GLS"></a>

L

line number

**line** **number**. A number located at either the beginning or the end of a record (line) that can be used during editing to refer to that line. See *prompting*.

load

**load**. In installation and service, to move files from tape to disk, auxiliary storage to main storage, or minidisks to virtual storage within a virtual machine.

load map

**load** **map**. A map containing the storage addresses of control sections and entry points of a program loaded into storage.

loadable unit

**loadable** **unit**. A portion of a product that can be installed independently of the rest of the product, but is serviced as part of the product.

loader

**loader**. A routine, commonly a computer program, that reads data into main storage.

Local disk

**Local** **disk**. In VMSES/E, a minidisk or SFS directory containing local modifications, customized files, and any circumventive service.

local modification

**local** **modification**. Any change applied to a product other than a PTF See circumventive service and user modification.

local service

**local** **service**. Changes manually applied to a product or component (that is, not using the program update service or corrective service procedures). See *circumventive* *service* and *user* *modification*.

local tracking number

**local** **tracking** **number**. The unique identifier assigned to a local modification. The local tracking number is used in the file type of update files and in the update file identification records of auxiliary control files. Each installation has its own system of local tracking numbers.

Local string

**Local** **string**. In VMSES/E, the set of Local disks.

logical record

logical record. A formatted record that consists of a 2-byte logical

**record?** **length** and a data field of variable length.

logical saved segment

**logical** **saved** **segment**. A portion of a physical saved segment that CMS can manipulate. Each logical segment can contain different types of program objects, such as modules, text files, execs, callable services libraries, language repositories, user-defined objects, or a single minidisk directory. A system segment identification file (SYSTEM SEGID) associates a logical saved segment to the physical saved segment in which it resides. See *physical* *saved* *segment* and *saved* *segment*.

logoff

**logoff**. The procedure by which a user ends a terminal session.

logon

**logon**. The procedure by which a user begins a terminal session.

low common storage

**low** **common** **storage**. GCS common storage that resides below the 16MB line. See *common* *storage*..

<a id="GLS"></a>

<a id="GLS"></a>

M

machine

**machine**. A synonym for a virtual machine running under the control of VM/ESA.

macro

**macro**. Synonym for *macrodefinition* and *macroinstruction*.

macro library

**macro** **library**. A library of macrodefinitions.

macrodefinition

**macrodefinition**. A set of statements that defines the name of, format of, and conditions for generating a sequence of assembler language statements from a single source statement. Synonymous with *macro*.

macroinstruction

**macroinstruction**. In assembler language programming, an assembler language statement that causes the assembler to process a predefined set of statements called a macrodefinition. The statements usually produced from the macrodefinition replace the macroinstruction in the program. Synonymous with *macro*.

MANUAL

**MANUAL**. This status, listed in the service-level build status table, indicates that the object requires MANUAL processing.

map

**map**. In CMS, the file that contains a CMS output listing, such as (1) a list of macros in the MACLIB library, including macro size and location within the library; (2) a listing of the directory entries for the DOS/VS system or private source, relocatable, or core image libraries; (3) a linkage editor map for CMS/DOS programs; and (4) a module map containing entry point locations.

mapping

**mapping**. To show relationships between objects.

MB

**MB**. Megabyte.

MDISK

**MDISK**. (1) Another name for minidisk. (2) The VM directory statement that describes a user's storage space.

megabyte (MB)

**megabyte** **(MB)**. 1,048,576 bytes.

member saved segment

**member** **saved** **segment**. A saved segment that begins and ends on a page boundary. It can be a member in up to 64 segment spaces and is accessed either by the segment space name or by its own name. Contrast with *discontiguous* *saved* *segment*. See *saved* *segment*, *segment*, and *segment* *space*.

memo-to-users

**memo-to-users**. A file provided on a service tape that contains specific service information for a product.

merge

**merge**. When receiving files from a service tape using VMFMRDSK, the process of moving existing service files from each minidisk or SFS directory in the target string to the minidisk or directory that contains the previous service level. The result is that the primary target minidisk or directory is left empty and ready to receive the latest service.

message

**message**. Data sent from a source application to a target application program in a conversation. See also *message* *text*, *message* *key*, and *message* *header*.

minidisk

**minidisk**. A logical subdivision (or all) of a physical disk pack that has its own virtual device address, consecutive virtual cylinders (starting with virtual cylinder 0), and a VTOC or disk label identifier. Each user virtual disk is preallocated and defined by a VM/ESA directory entry as belonging to a user.

minidisk directory

**minidisk** **directory**. Synonym for *CMS* *minidisk* *file* *directory*.

module

**module**. (1) A unit of a software product that is discretely and separately identifiable with respect to modifying, compiling, and merging with other units, or with respect to loading and execution. For example, the input to, or output from, a compiler, the assembler, the linkage editor, or an exec routine. (2) A nonrelocatable file whose external references have been resolved..

<a id="GLS"></a>

N

named saved system (NSS)

**named** **saved** **system** **(NSS)**. A copy of an operating system that a user has named and saved in a file. The user can load the operating system by its name, which is more efficient than loading it by device number. See *discontiguous* *saved* *segment*, *member* *saved* *segment*, *saved* *segment*, *segment* *space*, and *system* *data* *file*.

negative prerequisite

**negative** **prerequisite**. In VMSES/E, a product that cannot exist on a system at the same time as another product.

NSS

**NSS**. Named saved system.

nucleus

**nucleus**. The part of CP and CMS resident in main storage..

<a id="GLS"></a>

O

object

**object**. In VMSES/E, a usable form defined in build lists. A built part of a product. A product consists of many objects, for example, nuclei, modules, execs, help files, and macro libraries. See usable forms. Compare *subject*.

object code

**object** **code**. Compiler or assembler output that is executable machine code or is suitable for more processing to produce executable machine code. Contrast with *source* *code*.

object module

**object** **module**. A module that is the output of an assembler or a compiler and is input to a linkage editor.

operand

**operand**. Information entered with a command name to define the data on which a command processor operates and to control the execution of the command processor.

out-of-component requisite

**out-of-component** **requisite**. In VMSES/E, at the service-level, a PTF from another product that must be applied to that product in order for this PTF to function properly.

overhead

**overhead**. The additional processor time charged to each virtual machine for the CP functions needed to simulate the virtual machine environment and for paging and scheduling time.

override

**override**. Synonym for *component* *parameter* *override*.

override area

**override** **area**. Synonym for *component* *override* *area*.

override file

**override** **file**. Synonym for *class* *override* *file* and *product* *parameter* *override* *file*.

override $PPF

**override** **$PPF**. Synonym for *override* *product* *parameter* *file*..

<a id="GLS"></a>

<a id="GLS"></a>

P

pack

**pack**. A set of flat, circular recording surfaces that a disk storage device uses. A disk pack.

page

**page**. A fixed-length block that has a virtual address and can be transferred between real storage and auxiliary storage.

parameter

**parameter**. A variable that is given a constant value for a specified application and that may denote the application.

parameter driven installation (PDI)

**parameter** **driven** **installation** **(PDI)**. A product format that lets you specify a product installation location, specify installation related parameters, install multiple copies of a product, and select a default installation path.

part

**part**. A CMS file provided on a product tape or service tape as input to the build process. See *build*. A part is the smallest serviceable unit of a component.

part handler

**part** **handler**. An exec provided by VMSES/E that builds a specific type of object or loads parts from service media.

parts catalog

**parts** **catalog**. In VMSES/E, a set of Software Inventory files that catalog all parts of a product on a minidisk or SFS directory. All product parts are cataloged when they are loaded onto the system, when they are generated, and when they are moved.

password

**password**. In computer security, a string of characters known to the computer system and a user, who must specify it to gain full or limited access to a system and to the data stored within it.

patch

**patch**. A circumventive service change applied directly to object code in a text deck in a nucleus.

patch update file

**patch** **update** **file**. A file containing a single patch. The file can also specify requisites for applying the patch.

PDI

**PDI**. Parameter driven installation.

PF key

**PF** **key**. Programmed function key.

physical saved segment

**physical** **saved** **segment**. One or more pages of storage that have been named and retained on a CP-owned volume (DASD). Once created, it can be loaded within a virtual machine's address space or outside a virtual machine's address space. Multiple users can load the same copy. A physical saved segment can contain one or more logical saved segments. A system segment identification file (SYSTEM SEGID) associates a physical saved segment to its logical saved segments. See *logical* *saved* *segment* and *saved* *segment*.

PPF

**PPF**. Product parameter file.

preferred auxiliary file

**preferred** **auxiliary** **file**. In CMS, an auxiliary file that applies to a particular version of a source module to be updated, if multiple versions of the module exist.

preferred virtual machine

**preferred** **virtual** **machine**. A particular virtual machine that has one or more of the performance options assigned to it.

prefix area

**prefix** **area**. The five left-most positions on the XEDIT full-screen display, in which prefix subcommands or prefix macros can be entered. See *prefix* *macros* and *prefix* *subcommands*.

prefix macros

**prefix** **macros**. XEDIT macros entered in the prefix area of any line on a full-screen display. See *prefix* *area*.

prefix subcommands

**prefix** **subcommands**. XEDIT subcommands entered in the prefix area of any line on a full-screen display. See *prefix* *area*.

prerequisite

**prerequisite**. In VMSES/E, at the system-level, a product that must be installed before another product can be installed. At the service-level, a PTF that must be installed before another product can be installed.

prerequisite change

**prerequisite** **change**. A change that must be applied to the system before another change can be applied. For example, change2 lists change1 as a prerequisite. This indicates that the user must apply change1 before applying change2.

preventive service

**preventive** **service**. The application of all PTFs from a PUT or RSU. Contrast with selective preventive service. See program update tape and product service upgrade.

primary system operator privilege class

**primary** **system** **operator** **privilege** **class**. The CP privilege class A user. This operator has primary control over the VM/ESA system and can enable and disable teleprocessing lines, lock and unlock pages, force users off the VM/ESA system, issue warning messages, query, and set (and reset) performance options for selected virtual machines, and invoke VM/ESA accounting. If the current primary system operator logs off, the next class A user to log on becomes the primary system operator.

private storage

**private** **storage**. A combination of application code and GCS code available to only one particular virtual machine. No virtual machine can access or share another's private storage area.

privilege class

**privilege** **class**. One or more classes assigned to a virtual machine user in a VM/ESA directory entry; each privilege class specified lets a user access a logical subset of the CP commands. There are nine IBM-defined privilege classes that correspond to specific administrative functions. They are: Class A - primary system operator Class B - system resource operator Class C - system programmer Class D - spooling operator Class E - system analyst Class F - service representative Class G - general user Class H - reserved for IBM use Class Any - available to any user. The privilege classes can be changed to meet the needs of an installation. See *class* *authority* and *user* *class* *restructure* *(UCR)*.

privileged program

**privileged** **program**. In GCS, a program called by a GCS application that operates in supervisor state and uses privileged functions. A privileged program is one that meets either of the following requirements: It runs in an authorized virtual machine. It is called through the AUTHCALL facility. *Synonymous* with authorized *program*. Contrast with *nonprivileged* *program*.

process

**process**. (1) A systematic sequence of operations to produce a specified result. A process is usually logical, not physical. (2) In CMS Multitasking, a collection of threads performing related work. A process can have resources associated with it, such as storage subpools, queues, open files, and APPC conversations. All threads in a process have equal access to the resources associated with the process.

PRODPART file

**PRODPART** **file**. VMSES/E uses information in this file, included on a product's install tape, to update entries in the system-level Software Inventory each time a product is loaded onto your system.

product

**product**. Any separately installable software program, whether supplied by IBM or otherwise, distinct from others and recognizable by a unique identification code. The product identification code is unique to a given product, but does not identify the release level of that product.

product identifier (prodid)

**product** **identifier** **(prodid)**. The product identifier is the 7- or 8-alphanumeric character identifier assigned to the product by IBM.

product parameter file (PPF)

**product** **parameter** **file** **(PPF)**. A file containing installation and service parameters for a product: control options, minidisk and SFS directory assignments, and component part type/function lists.

product parameter override file

**product** **parameter** **override** **file**. A file containing one or more component override areas.

product processing exit

**product** **processing** **exit**. An interface used by program products to perform additional product installation tasks.

product service upgrade (PSU)

**product** **service** **upgrade** **(PSU)**. A procedure used to upgrade the service level of a product or component using a recommended service upgrade (RSU) tape.

product tape

**product** **tape**. One of a set of tapes containing individual components or products to load and build.

PROFILE EXEC

**PROFILE** **EXEC**. A special EXEC procedure with a file name of PROFILE that a user can create. The procedure is usually executed immediately after CMS is loaded into a virtual machine (also known as IPL CMS).

program temporary fix (PTF)

program temporary fix (PTF). Code changes needed to correct a problem reported in an APAR. The corrected code is included in later releases. A PTF contains one or more APAR fixes. For object-maintained parts that are

changed, the PTF includes replacement parts. For source-maintained parts that are changed, the PTF includes update files and replacement parts. Each PTF is unique to a given release of a product. If the same problem occurs? in multiple releases of a product, a separate PTF is defined for each release.

program update service

**program** **update** **service**. Receiving service from a PUT or RSU, applying all or some of the changes, and rebuilding the serviced parts. See preventive service and selective preventive service.

program update tape (PUT)

program update tape (PUT). A tape containing a customized collection of

service tapes? (preventive service) to match the products listed in a customer's ISD (IBM Software Distribution) profile. Each PUT contains cumulative service for the customer's products back to earlier release levels of the product still supported. The tape is distributed to authorized customers of the products at scheduled intervals or on request.

programmed function (PF) key

programmed function (PF) key. On a terminal, a key that can do various

functions? selected by the user or determined by an application program.

prompt

**prompt**. A displayed message that describes required input or gives operational information.

prompting

**prompting**. An interactive technique that lets the program guide the user in supplying information to a program. The program types or displays a request, question, message, or number, and the user enters the desired response. The process is repeated until all the necessary information is supplied.

PSU

**PSU**. product service upgrade

PTF

**PTF**. Program temporary fix.

PTF number

**PTF** **number**. A number assigned by service organizations that uniquely identifies a PTF; for example, IBM uses UVNNNNN for a VM-unique product, and UPnnnnn for a cross-system product. PTFs for different products or different releases of a product have different numbers.

PUT

**PUT**. Program update tape..

<a id="GLS"></a>

<a id="GLS"></a>

R

R/O

**R/O**. Read-only.

R/W

**R/W**. Read/write.

rdev

**rdev**. The real device address of an I/O device.

reach-ahead service

**reach-ahead** **service**. Corrective service or local service that has been applied to a product but is not available on a program update tape, product service upgrade, or other service vehicle.

read authority

**read** **authority**. The authority to read the contents of a file without being able to change them. For a directory, read authority lets the user view the names of the objects in the directory.

read-only access

**read-only** **access**. An access mode associated with a virtual disk or SFS directory that lets a user read, but not write or update, any file on the disk or SFS directory.

read/write access

read/write access. An access mode associated with a virtual disk or SFS

directory? that lets a user read and write any file on the disk or SFS directory (if write authorized).

real address

**real** **address**. The address of a location in real storage or the address of a real I/O device.

receive

**receive**. (1) Bringing into the specified buffer data sent to the user's virtual machine from another virtual machine or from the user's own virtual machine. (2) To load service files from a service tape. (3) In CMS Multitasking interprocess communication, the action of retrieving a message from a queue.

receive ID

**receive** **ID**. A 7- or 8- alphanumeric character identifier that is used to name the Software Inventory files created during receive processing.

receive status table

**receive** **status** **table**. The Software Inventory table that contains the relationship between a product and the $PPF file used to install it. It also identifies what products of PTFs have been received or committed. The file type of the system level inventory table is SYSRECS and the file type of the service level inventory table is

<a id="VRECS"></a>

RECEIVED

**RECEIVED**. This status, listed in the receive status table, indicates that a product or PTF has been RECEIVED on the system.

Recommended Service Upgrade (RSU) tape

**Recommended** **Service** **Upgrade** **(RSU)** **tape**. A tape containing preventive service for upgrading the current release of a VM/ESA system once it has been installed.

recomp

**recomp**. To change the number of cylinders or blocks on the disk that are available to you.

regression

**regression**. Causing serviced parts to go back to earlier levels. This can occur when applying changes from a PUT to parts updated by corrective service or user modifications.

Remote Spooling Communications Subsystem Networking (RSCS)

**Remote** **Spooling** **Communications** **Subsystem** **Networking** **(RSCS)**. An IBM licensed program and special-purpose subsystem that supports the reception and transmission of messages, files, commands, and jobs over a computer network.

REMOVED

**REMOVED**. This status, listed in the apply status table, indicates that the PTF has been REMOVED from the system.

replacement parts

**replacement** **parts**. See serviceable parts.

replacement service

**replacement** **service**. Servicing a part by replacing the part with a new one.

requisite

**requisite**. The requirements of a product or PTF.

requisite relationships

**requisite** **relationships**. The interrelated requirements of a product or PTF.

requisite table

**requisite** **table**. The Software Inventory table that contains the requisite relationships between products, in the system level, and PTFs in the service level. The file type of the system level inventory table is SYSREQT and the file type of the service level inventory table is

<a id="VREQT"></a>

resource

**resource**. A program, a data file, a specific set of files, a device, or any other entity or a set of entities that the user can uniquely identify for application program processing in a VM system.

REXX exec

**REXX** **exec**. An EXEC procedure or XEDIT macro written in the REXX language and processed by the REXX/VM Interpreter. Synonymous with *REXX* *program*.

REXX program

**REXX** **program**. Synonym for *REXX* *exec*.

RSCS

**RSCS**. Remote Spooling Communications Subsystem Networking.

RSU

**RSU**. Recommended Service Upgrade.

<a id="GLS"></a>

<a id="GLS"></a>

S

saved segment

**saved** **segment**. A segment of storage that has been saved and assigned a name. The saved segments can be physical saved segments that CP recognizes or logical saved segments that CMS recognizes. The segments can be loaded and shared among virtual machines, which helps use real storage more efficiently, or a private, nonshared copy can be loaded into a *virtual* machine. See logical *saved* *segment* and *physical* *saved* *segment*.

saved system

**saved** **system**. A special nonrelocatable copy of a virtual machine's virtual storage and associated registers kept on a CP-owned disk and loaded by name instead of by I/O device address. Loading a saved system by name substantially reduces the time it takes to IPL the system in a virtual machine. Also, a saved system such as CMS can also share one or more 1MB segments of reenterable code in real storage between virtual machines. This reduces the cumulative real main storage requirements and paging demands of such virtual machines.

screen

**screen**. An illuminated display surface; for example, the display surface of a CRT. Synonymous with *physical* *screen*.

SDO

**SDO**. System delivery offering.

secondary user

**secondary** **user**. When a user is disconnected -- that is, has no virtual console on line -- a secondary user can be designated to receive the disconnected user's console messages and to enter commands to the disconnected user's console.

segment

**segment**. In System/370 architecture, 64KB of virtual storage. In 370-XA, ESA/370, ESA/390, and ESA/XC architecture, 1MB of virtual storage.

segment space

**segment** **space**. A saved segment is composed of up to 64 member saved segments accessed by a single name. A segment space occupies one or more architecturally-defined segments. It begins and ends on a 1MB boundary. A user with access to a segment space has access to all of its members. See *discontiguous* *saved* *segment*, *member* *saved* *segment*, *saved* *segment*, and *segment*.

select data file

select data file. In VMSES/E, a file containing a list of the parts

serviced by the VMFAPPLY EXEC. The VMFAPPLY EXEC updates this file with a time stamp and a list of parts that were serviced. The VMFBLD EXEC checks the select data file for build requirements and updates the objects that are affected by service to a status of 'SERVICED' in the service-level build status table. The select data file is named appid $SELECT, where appid? is the apply ID. See apply ID.

selective preventive service

**selective** **preventive** **service**. The selective application of PTFs from a PUT or RSU. Contrast with preventive service.

separator

**separator**. Synonym for *delimiter*.

server

**server**. The general name for a virtual machine that provides a service for a requesting virtual machine.

service

**service**. Changing a product after installation. See *corrective* *service,* *local* *service,* and *program* *update* *service*.

service level

**service** **level**. The PTF and preventive service level that is associated with the testing level and support level of an orderable product function.

service level inventory

**service** **level** **inventory**. see *service-level* *Software* *Inventory*.

service-level Software Inventory

**service-level** **Software** **Inventory**. In VMSES/E, the level of the Software Inventories that contains: requisite relationships between PTFs, the status of PTFs installed, the service level of each part of the product and, the status of objects built for the product.

service machine

**service** **machine**. A virtual machine running a program that provides system-wide services.

service tape

**service** **tape**. A tape containing service changes for one or more products. See *corrective* *service* *tape* and *program* *update* *tape* *(PUT)*.

service virtual machine

**service** **virtual** **machine**. A virtual machine that provides a system service such as accounting, error recording, monitoring, or that provided by a supported licensed program.

serviceable parts

serviceable parts. The individual parts of a product that can be serviced

separately. A serviceable part has the file name of the source or **replacement** **part?** and a file type in the form tttnnnnn, where ttt is a unique three-character abbreviation for the part type and nnnnn is the PTF number. Serviceable parts are maintained by both source updates and replacement service.

SERVICED

**SERVICED**. This status, listed in the service-level build status table, indicates that the object has been SERVICED but not built.

SFS

**SFS**. Shared file system.

SFS directory

**SFS** **directory**. A group of files. SFS directories can be arranged to form a hierarchy in which one directory can contain one or more subdirectories as well as files.

Shared File System (SFS)

**Shared** **File** **System** **(SFS)**. A part of CMS that lets users organize their files into groups known as *directories* and selectively share those files and directories with other users.

shared segment

**shared** **segment**. A feature of a saved system or physical saved segment that lets one or more segments of reentrant code or data in real storage be shared among many virtual machines. For example, if a saved CMS system was generated, the CMS nucleus is shared in real storage among all CMS virtual machines loaded by name; that is, every CMS machine's segment of virtual storage maps to the same 1MB of real storage. See *discontiguous* *saved* *segment* and *saved* *system*.

shared system

**shared** **system**. See *saved* *system* and *shared* *read-only* *system* *residence* *disk*.

simultaneous peripheral operations online (SPOOL)

**simultaneous** **peripheral** **operations** **online** **(SPOOL)**. (1) (Noun) An area of auxiliary storage defined to temporarily hold data during its transfer between peripheral equipment and the processor. (2) (Verb) To use auxiliary storage as a buffer storage to reduce processing delays when transferring data between peripheral equipment and the processing storage of a computer.

single user group

**single** **user** **group**. The concept in GCS of a virtual machine that runs applications that do not require group communications. This allows an application to run without the overhead of group initialization and multiple virtual machines. Multiple users can IPL the same saved system if it had been built for a single user environment. See *virtual* *machine* *group*.

SNA

**SNA**. Systems Network Architecture.

soft requisite

**soft** **requisite**. The subset of a PTF's requisite that is not a hard requisite. A PTF has a soft requisite for another PTF if it affects any of the same modules. The relationship exists because the pre-built replacement parts that are shipped with PTFs are built with all prior PTFs.

software inventory management

**software** **inventory** **management**. Utilities provided by VMSES/E that provide a standard interface to the system level inventories, service level inventories, tool control statements (TCS), product parameter file (PPF), and file type abbreviation table.

software product

**software** **product**. Any software supplied by IBM or an Original Equipment Manufacturer (OEM), or user written programs. The term includes program offerings and program products (PPs).

source code

**source** **code**. The input to a compiler or assembler, written in a source language. Contrast with *object* *code*.

source file

**source** **file**. A file that contains source statements for such items as high-level language programs and data description specifications.

source product parameter file

**source** **product** **parameter** **file**. In VMSES/E, a file supplied with a product containing: recommended values for the options that control VMSES/E processing for the product, installation and service tape formats, and the list of build lists used to build the product. The file name of the source product parameter file matches the prodid of the product and the file type is $PPF. Source PPFs.

source update

**source** **update**. A change to the original assembler code provided with a product. VM source code is contained in files with a file type of ASSEMBLE. To update an ASSEMBLE file, the user creates update files containing control statements that describe the changes to be made.

source update file

**source** **update** **file**. A file containing a single change to a statement in a source file. The file can also include requisite information for applying the change. Synonymous with *update* *file*.

SPOOL

**SPOOL**. Simultaneous peripheral operations online.

spool file

**spool** **file**. A collection of data along with CCWs for processing on a unit record device. Contrast with *system* *data* *file*.

spool ID

**spool** **ID**. A spool file identification number automatically assigned by CP when the file is closed. The spool ID number can be from 0001 to 9900; it is unique for each spool file. To identify a given spool file, a user must specify the owner's user ID, the virtual device type, and the spool ID.

spooling

**spooling**. The processing of files created by or intended for virtual readers, punches, and printers. The spool files can be sent from one virtual device to another, from one virtual machine to another, and to real devices. See *virtual* *console* *spooling*.

stand-alone dump

**stand-alone** **dump**. A dump acquired without regular system functions. For example, to obtain a CP dump when the regular system is unable to dump the machine, the stand-alone dump facility gets a CP stand-alone dump.

string

**string**. A group of minidisks defined for a specific function in the product parameter file, for example, the BASE2 string, which holds source code.

sub hard requisite

**sub** **hard** **requisite**. In VMSES/E, a sub hard requisite is a hard requisite of an explicitly defined requisite.

sub if-requisite

**sub** **if-requisite**. In VMSES/E, a sub if-requisite is an if-requisite of an explicitly defined requisite.

subcommand

**subcommand**. The commands of processors such as EDIT or XEDIT that run under CMS.

subdirectory

**subdirectory**. Any SFS directory below a user's top directory. The CREATE DIRECTORY command creates subdirectories. There can be up to eight levels of subdirectories with no limit on the number of them at each level, other than overall DASD space limits. Each level of a subdirectory is an additional identifier of up to 16 characters that is appended to next higher level subdirectory.

subrequisite

**subrequisite**. A subrequisite is a prerequisite or corequisite or an explicitly defined requisite. The requisite of requisites.

SUPED

**SUPED**. This status, listed in the service-level apply status table, indicates that the PTF has been superseded.

supersede

**supersede**. When a PTF supersedes another PTF, it includes all of the APARs, parts and requisite relationships of the PTF it supersedes.

syntax

**syntax**. The rules for the construction of a command or program.

system administrator

**system** **administrator**. The person responsible for maintaining a computer system.

system DDR tape

**system** **DDR** **tape**. A tape containing the image of a built system for each type of DASD.

system delivery offering (SDO)

system delivery offering (SDO). A VM/ESA package that includes a subset

of all VM products or components. This package has a single point of order? and delivery, is refreshed periodically, and is installed from one logical tape. All products or components included with the package, and their requisite relationships, are tested to ensure the package functions as a system.

System disk

**System** **disk**. In VMSES/E, a minidisk or SFS directory containing other products that are required during service.

system level inventory

**system** **level** **inventory**. See *system-level* *Software* *Inventory*.

system-level Software Inventory

**system-level** **Software** **Inventory**. Level of the Software Inventories that contains: requisite relationships between products or components, the status of the product or component on the system, mapping of product identifier to the name of the product parameter file used during installation, and mapping of PTF file type abbreviation to real CMS file type.

system offering

**system** **offering**. A package containing VM/SP and associated products.

system profile

**system** **profile**. An EXEC (SYSPROF) that resides in a saved system or on a system disk and called by CMS initialization. It contains some initialization functions, and provides a means for installations to override the default CMS environment by tailoring the exec to suit the installation.

system restart

**system** **restart**. The restart that allows reuse of previously initialized areas. System restart usually requires less time than IPL. See *warm* *start*.

Systems Network Architecture (SNA)

**Systems** **Network** **Architecture** **(SNA)**. The description of the logical structure, formats, protocols, and operational sequences for transmitting information units through and controlling the configuration and operation of networks.

System string

**System** **string**. In VMSES/E, the set of System disks..

<a id="GLS"></a>

<a id="GLS"></a>

T

T-disk

**T-disk**. Synonym for *temporary* *disk*.

tailorable file

**tailorable** **file**. any source level product file that requires user input in order for the product to work correctly. (An example is a PROFILE EXEC.)

tailorings

**tailorings**. Changes made to a source level product file to customize it for your own environment.

tape descriptor file

**tape** **descriptor** **file**. A file containing a directory of the products on a service tape.

tape document

**tape** **document**. A document describing the service procedure for a service tape.

target

**target**. One of many ways to identify a line to be searched for by XEDIT. A target can be specified as an absolute line number, a relative displacement from the current line, a line name, or a string expression.

Target disk

**Target** **disk**. In VMESE/E, a minidisk of SFS directory to which tape files are received on which the objects are built.

Target string

**Target** **string**. In VMSES/E, the set of Target disks.

task

**task**. A basic unit of work used for the execution of a program or a system function.

temporary disk

temporary disk. An area on a DASD available to the user for newly created

or stored files until logoff, at which time the area is released. **Temporary** **disk** space is allocated to the user during logon or when entering the CP DEFINE command. Synonymous with *T-disk*.

temporary product parameter file

**temporary** **product** **parameter** **file**. In VMSES/E, the output of the VMFOVER EXEC. The file name is either the file name of the last override product parameter file in the chain of overrides, or the file name of the source product parameter file. The file type is $PPFTEMP.

terminal

**terminal**. A device, usually equipped with a keyboard and a display, capable of sending and receiving information.

text deck

**text** **deck**. An object-code file that must be additionally processed to produce executable machine code.

text library

**text** **library**. A CMS file that contains relocatable object modules and a directory that indicates the location of each of these modules within the library.

time stamp

**time** **stamp**. A record containing the TOD clock value stored in its internal 32-bit binary format.

time-of-day (TOD) clock

**time-of-day** **(TOD)** **clock**. A hardware feature required by VM/ESA. The TOD clock is incremented once every microsecond, and provides a consistent measure of elapsed time suitable for the indication of date and time; it runs regardless of the processor state (running, wait, or stopped).

TOD clock

**TOD** **clock**. Time-of-day clock.

token

**token**. An eight-character symbol created by the CMS EXEC processor when it scans an EXEC procedure or EDIT macro statements. Symbols longer than eight characters are truncated to eight characters..

<a id="GLS"></a>

U

update file

**update** **file**. Synonym for *source* *update* *file*.

update service

**update** **service**. Servicing a part by applying a change to a source file statement, then assembling or compiling the source file to produce a new object file.

usable form

**usable** **form**. In VMSES/E, a part of a product whose level cannot be identified from its file name or file type, the final objects which make up the product, a source file with the file type ASSEMBLE. If the level can be identified from its file name the part is referred to as a serviceable part.

usable form product parameter file

**usable** **form** **product** **parameter** **file**. Product parameter files used by the majority of VMSES/E execs. The file name matches the file name of either the last override product parameter file in the chain of overrides, or the file name of the source product parameter file if there are no overrides. The file type is PPF.

user

**user**. Anyone who requests the services of a computing system.

user class

**user** **class**. A privilege category assigned to a virtual machine user in the user's directory entry; each class specified allows access to a logical subset of all the CP commands. See *privilege* *class*.

user exit

**user** **exit**. An interface to VM/ESA that can be used by an application program. Generally, a user exit affects only the particular application specifying the exit and is run as part of the application program.

user ID

**user** **ID**. User identification.

user memo

**user** **memo**. (1) At the system-level, special instructions for installing a product, and (2) at the service-level, special instructions for installing a PTF.

user modification

**user** **modification**. Any change that a user originates for a product or component..

<a id="GLS"></a>

<a id="GLS"></a>

V

vaddr

**vaddr**. Virtual address.

variable symbol

**variable** **symbol**. In an EXEC procedure, a symbol beginning with an ampersand (&) character, the value of which is assigned by the user, or sometimes by the VM/REXX interpreter, the EXEC 2 processor, or CMS EXEC processor. The value of a variable symbol can be tested and changed using control statements. See *special* *variable*.

version vector table

**version** **vector** **table**. The Software Inventory table that identifies which PTFs have been applied to each part of the product and the current level of each part. The file type of the service level inventory table is VVTlvlid. The lvlid may be unique for each level of service the customer has installed for a product or component. It corresponds directly to each AUX level in the control file. The system level inventory does not contain this table.

virtual address

**virtual** **address**. The address of a location in virtual storage. A virtual address must be translated into a real address to process the data in processor storage.

virtual console

**virtual** **console**. A console simulated by CP on a terminal such as a 3270. The virtual device type and I/O address are defined in the VM/ESA directory entry for that virtual machine.

virtual console spooling

**virtual** **console** **spooling**. The writing of console I/O on disk as a printer spool file instead of, or in addition to, having it typed or displayed at the virtual machine console. The console data includes messages, responses, commands, and data from or to CP and the virtual machine operating system. The user can invoke or terminate console spooling at any time. When the console spool file is closed, it becomes a printer spool file. Synonymous with *console* *spooling*.

virtual disk

**virtual** **disk**. A logical subdivision (or all) of a physical disk storage device that has its own address, consecutive storage space for data, and an index or description of the stored data so that the data can be accessed. A virtual disk is also called a minidisk. See *disk*.

virtual machine (VM)

**virtual** **machine** **(VM)**. A functional equivalent of a computing system. In VM/ESA, virtual machines can simulate the System/370, 370-XA, ESA/370, and ESA/390 functions. In addition, on ESA/390 systems, the XC virtual machine architecture is available. Each virtual machine is controlled by an operating system. VM controls the concurrent execution of several virtual machines on an actual processor complex. See *370* *virtual* *machine* *XA* *virtual* *machine*, *ESA* *virtual* *machine*, and *XC* *virtual* *machine*.

virtual machine group

**virtual** **machine** **group**. The concept in GCS of two or more virtual machines associated with each other through the same named system (for example, IPL GCS1). Virtual machines in a group share common read/write storage and can communicate with one another through facilities provided by GCS. Synonymous with *group*. See *single* *user* *group*.

Virtual Machine/Enterprise Systems Architecture (VM/ESA)

**Virtual** **Machine/Enterprise** **Systems** **Architecture** **(VM/ESA)**. An IBM licensed program that manages the resources of a single computer so that multiple computing systems appear to exist. Each virtual machine is the functional equivalent of a *real* machine.

virtual printer (or punch)

**virtual** **printer** **(or** **punch)**. A printer (or card punch) simulated on disk by CP for a virtual machine. The virtual device type and I/O address are usually defined in the VM/ESA directory entry for that virtual machine.

virtual storage

virtual storage. Storage space that can be regarded as addressable main

storage by the user of a computer system in which virtual addresses are mapped? into real addresses. The size of virtual storage is limited by the addressing scheme of the computing system and by the amount of auxiliary storage available, not by the actual number of main storage locations.

virtual=real area (V=R area)

**virtual=real** **area** **(V=R** **area)**. The part of real storage, starting with real page 1, where a virtual=real machine can execute. CP maintains control of real page zero; only page zero of the virtual=real machine is relocated. Only one virtual machine at a time can occupy the virtual=real area. The area must be defined during VM/ESA system generation to contain the largest virtual=real machine likely to run. See *virtual=real* *option*.

VM

**VM**. Virtual machine.

VM directory

**VM** **directory**. A CP disk file that defines each virtual machine's typical configuration: the user ID, password, regular and maximum allowable virtual storage, CP command privilege class or classes allowed, dispatching priority, logical editing symbols to be used, account number, and CP options desired. Synonymous with *CP* *directory*.

VM/ESA

**VM/ESA**. Virtual Machine/Enterprise Systems Architecture.

VMLIB

**VMLIB**. The name of the CSL supplied with VM/ESA and that contains routines to do various VM functions.

VMSES

**VMSES**. A component of VM in VM/ESA Rel. 1.0 that provides the tools for installing and servicing the various components of the VM product.

VMSES/E

**VMSES/E**. Virtual Machine Serviceability Enhancements Staged/Extended.

VMSES/E

**VMSES/E**. A component of VM, first shipped in VM/ESA Rel. 1.1, that provides the tools for installing and servicing the various components of the VM product. It is also the strategic installation and service tool for all of the other products that run on VM/ESA platforms.

VMSES/E installation/service tool

**VMSES/E** **installation/service** **tool**. Consists of two VMSES/E user interfaces, VMFINS and VMFSIM, all of the VMSES/E commands, and the service-level and system-level Software Inventories. Synonymous with *VMSES/E*.

volid

**volid**. Volume identifier.

volume identifier (volid)

**volume** **identifier** **(volid)**. The volume identification label for a disk..

<a id="GLS"></a>

<a id="GLS"></a>

W

warm start

**warm** **start**. (1) The result of an IPL that does not erase previous system data. (2) The automatic reinitialization of the VM/ESA control program that occurs if the control program cannot continue processing. Closed spool files and the VM/ESA accounting information are not lost. Contrast with *checkpoint* *(CKPT)* *start,* *cold* *start*, and *force* *start*.

window

**window**. An area on the physical screen where virtual screen data can be displayed. Windowing lets the user do such functions as defining, positioning, and overlaying windows; scrolling backward and forward through data; and writing data into virtual screens.

write authority

**write** **authority**. The authority to read or change the contents of a file or directory. Write authority implies read authority..

<a id="GLS"></a>

X

XA mode

**XA** **mode**. A GCS mode of operation on ESA that uses the full capabilities of the Extended Systems Architecture.

XEDIT

**XEDIT**. The CMS facility, containing the XEDIT command and XEDIT subcommands and macros, that lets a user create, change, and manipulate CMS files.

XEDIT macro

XEDIT macro. (1) A procedure defined by a frequently used command

sequence to do a commonly required editing function. A user creates the macro to save repetitious reentering of the sequence, and invokes the entire procedure by entering a command (that is, the macro file's file name). The procedure can consist of a long sequence of XEDIT commands and subcommands or both, and CMS and CP commands or both, along with REXX or EXEC? 2 control statements to control processing within the procedure. (2) A CMS file with a file type of *XEDIT*..

<a id="GLS"></a>

Y

Y-STAT

**Y-STAT**. A block of storage that contains the FSTs associated with file mode Y. The FSTs are sorted so that a binary search can search for files. The Y-STAT usually resides in the CMS nucleus so it can be shared. Only files with file mode of 2 will have their associated FSTs in the Y-STAT..

<a id="GLS"></a>

Z

zap

**zap**. To modify or dump an individual text file, using the ZAP command or the ZAPTEXT EXEC..

<a id="GLS"></a>

3

3262

**3262**. Refers to the IBM 3262 Printer, Models 1 and 11.

3270

**3270**. Refers to a series of IBM display devices, for example, the IBM 3275, 3276 Controller Display Station; 3277, 3278, and 3279 Display Stations; the 3290 Information Panel; and the 3287 and 3286 printers. A specific device type is used only when a distinction is required between device types. Information about display terminal usage also refers to the IBM 3138, 3148, and 3158 Display Consoles when used in display mode, unless otherwise noted.

3284

**3284**. Refers to the IBM 3284 Printer. Information on the 3284 also pertains to the IBM 3286, 3287, 3288, and 3289 printers, unless otherwise noted.

3380

**3380**. Refers to the IBM 3380 Direct Access Storage Device.

3390

**3390**. Refers to the IBM 3390 Direct Access Storage Device.

3422

**3422**. Refers to the IBM 3422 Magnetic Tape Subsystem.

3480

**3480**. Refers to the IBM 3480 Magnetic Tape Subsystem.

3490

**3490**. Refers to the IBM 3490 Magnetic Tape Subsystem.

370 mode

**370** **mode**. A GCS mode of operation on ESA that simulates 370 architecture.

370 virtual machine

**370** **virtual** **machine**. A virtual machine that simulates System/370 functions. Contrast with *XA* *virtual* *machine,* *ESA* *virtual* *machine,* and *XC* *virtual* *machine*.

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

9332

**9332**. Refers to the IBM 9332 Direct Access Storage Device, Model 400.

9335

**9335**. Refers to the IBM 9335 Direct Access Storage Device, Models A01 and B01.

9370

**9370**. Refers to a series of processors, namely the IBM 9371 Models 10, 12, and 14, the IBM 9373 Model 20, the IBM 9375 Models 40 and 60, the IBM 9377 Model 90, and other models..

<a id="GLS"></a>

---

[Previous](g-5.md) | [Index](README.md) | [Next](bibliography.md)
