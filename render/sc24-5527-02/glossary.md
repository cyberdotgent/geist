[Previous](g-5.md) | [Index](README.md) | [Next](bibliography.md)

---

# GLOSSARY Glossary

<a id="HDRGLOSSY"></a>

<a id="SPTGLOSS"></a>

For a complete list of VM/ESA terms and their definitions, see the [VM/ESA:](<DOCNUM/GC24-5518/CCONTENTS>) [Master Index and Glossary](<DOCNUM/GC24-5518/CCONTENTS>)\.

The list is also available in the online VM/ESA HELP Facility\. For example, to display the definition of "cms", enter:

`help glossary cms`

When you enter the HELP Facility's online glossary file, the definition of "cms" is displayed as the current line\. Once you are in the glossary file, you can simply search for the other terms\.

If you are unfamiliar with the HELP Facility, you can enter:

`help`

to display the main HELP Menu, or enter:

`help cms help`

for information about the HELP command\.

For more information on the HELP Facility, see the [VM/ESA: CMS User's](<DOCNUM/SC24-5460/CCONTENTS>) [Guide](<DOCNUM/SC24-5460/CCONTENTS>)\. For more about the HELP command, see the [VM/ESA: CMS Command](<DOCNUM/SC24-5461/CCONTENTS>) [Reference](<DOCNUM/SC24-5461/CCONTENTS>)\.

You can find additional information on IBM terminology in the *Dictionary of Computing*, SC20\-1699\.

<a id="GLS"></a>

```
       ___
      | A |
      |___|
```

<a id="GLS access mode"></a>

**access mode**\. A method VM/ESA uses to control user access to data files\. Access modes let the user read and write data to a file, or only read data from a file\. See *file mode*\.

<a id="GLS alias"></a>

**alias**\. A pointer to an SFS base file\. An alias can be in the same directory as the base file or in a different directory\. There must always be a base file for the alias to point to\. The alias references the same data as the base file\. Data is not moved or duplicated\.

<a id="GLS alphanumeric"></a>

**alphanumeric**\. A character set that contains letters, digits, and usually other characters, such as punctuation marks\.

<a id="GLS APAR"></a>

**APAR**\. Authorized program analysis report\.

<a id="GLS APAR number"></a>

**APAR number**\. The number that IBM assigns to an APAR and to the change resulting from it\.

<a id="GLS application program"></a>

**application program**\. A program written for or by a user that applies to the user's work, such as a program that does inventory control or payroll\.

<a id="GLS APPLIED"></a>

**APPLIED**\. This status, listed in the apply status table, indicates a product or program temporary fix has been APPLIED to the system\.

<a id="GLS apply"></a>

**apply**\. When servicing a product or component, to generate an auxiliary control structure from a PTF\.

<a id="GLS Apply disk"></a>

**Apply disk**\. In VMSES/E, a minidisk or SFS directory containing the files that describe the maintenance levels: the apply status table, AUX files, version vector tables, the select data file, and the build status table\.

<a id="GLS apply list"></a>

**apply list**\. A file listing PTFs applied to a product or component\.

<a id="GLS apply status table"></a>

**apply status table**\. The Software Inventory table that identifies what PTFs have been applied to the product\. The system level of the table identifies what product or component has been applied to the system\. The file type of the system level inventory table is SYSAPPS and the file type of the service level inventory table is SRVAPPS\.

<a id="GLS Apply string"></a>

**Apply string**\. In VMSES/E, the set of Apply disks\.

<a id="GLS area"></a>

**area**\. A term acceptable for DASD space when there is no need to differentiate between space on count\-key\-data devices and FB\-512 devices\. See *DASD space*\.

<a id="GLS assembler language"></a>

**assembler language**\. A source language that includes symbolic machine language statements in which there is a one\-to\-one correspondence with instruction formats and data formats of the computer\.

<a id="GLS authority"></a>

**authority**\. In SFS, the permission to access a file or directory\. You can have read authority or write authority \(which includes read authority\)\. You can also have file pool administration authority, which is the highest level of authority in a file pool\.

<a id="GLS authorized program analysis report ( APAR )"></a>

**authorized program analysis report \(APAR\)**\. An official request to the responsible IBM Change Team to look into a suspected problem with IBM code or documentation\. APARs describe problems giving conditions of failure, error messages, abend codes, or other identifiers\. They also contain a problem summary and resolution when applicable\. See *program temporary fix \(PTF\)*\.

<a id="GLS AUX file"></a>

**AUX file**\. Auxiliary control file\.

<a id="GLS auxiliary control file ( AUX file )"></a>

**auxiliary control file \(AUX file\)**\. A file that contains a list of file types of update files applied to a particular source file or to control the service level used during build\. See *control file* and *preferred auxiliary file*\. Synonymous with *auxiliary file*\.

<a id="GLS auxiliary file"></a>

**auxiliary file**\. Synonym for *auxiliary control file*\.

<a id="GLS AVS"></a>

**AVS**\. APPC/VM VTAM Support\.

<a id="GLS"></a>

```
       ___
      | B |
      |___|
```

<a id="GLS Base disk"></a>

**Base disk**\. In VMSES/E, a minidisk or SFS directory containing the original product code\.

<a id="GLS base file"></a>

**base file**\. The first occurrence of an SFS file\. It remains the base for the life of the file, even if the file has been renamed\. Aliases point to base files\.

<a id="GLS base file type"></a>

**base file type**\. In VMSES/E, the file type used for a serviceable part when there is no service\. The PTF number in the file type is set to "00000\." For example, EXC00000 would be the base file type for an exec\. See serviceable part\.

<a id="GLS Base string"></a>

**Base string**\. IN VMSES/E, the set of Base disks\.

<a id="GLS block"></a>

**block**\. \(1\) A unit of DASD space on FB\-512 devices\. For example, FB\-512 devices can be the IBM 9335, 9332, 9313, 3370, and 3310 DASD using fixed\-block architecture\. \(2\) In CMS Multitasking, to stop the execution of a thread until a function has been completed or a condition is satisfied\.

<a id="GLS Bpi"></a>

**Bpi**\. Bytes per inch\.

<a id="GLS bpi"></a>

**bpi**\. Bits per inch\.

<a id="GLS build"></a>

**build**\. In the installation and service of a product, to do the necessary steps to produce executable code or systems\. This is often called the *build process*\.

<a id="GLS BUILDALL"></a>

**BUILDALL**\. This status, shown in the service\-level build status table, indicates the user requested that an object be built with the ALL option on the VMFBLD command, and the object still needs to be built\.

<a id="GLS Build disk"></a>

**Build disk**\. In VMSES/E, a minidisk or SFS directory containing the running code for the product being serviced\.

<a id="GLS Build ID"></a>

**Build ID**\. A 1\- to 8\- alphanumeric character identifier \(*bldid*\) that is used to name the Software Inventory files created during build processing\. The user can change this value to define different maintenance levels\.

<a id="GLS build list"></a>

**build list**\. An EXEC file that names the parts included in an object being built\.

<a id="GLS build requisites"></a>

**build requisites**\. An object that is needed to build another object\. For example, when one object is built using another object, the latter is a build requisite of the former\. Also, if an object's build requisite is serviced, the object must be rebuilt after its build requisite is built\.

<a id="GLS build status table"></a>

**build status table**\. The Software Inventory table that identifies what products have been built, in the system level, and what individual objects have been generated for the product, in the service level\. The file type of the system level inventory table is SYSBLDS and the file type of the service level inventory table is SRVBLDS\.

<a id="GLS Build string"></a>

**Build string**\. The set of Build disks\.

<a id="GLS build - time requisites"></a>

**build\-time requisites**\. Product\(s\) that must be installed before a certain product can run correctly\.

<a id="GLS BUILT"></a>

**BUILT**\. This status, listed in the build status table, indicates that a product or object has been built on the system\.

<a id="GLS"></a>

```
       ___
      | C |
      |___|
```

<a id="GLS callable services library ( CSL )"></a>

**callable services library \(CSL\)**\. A package of CMS assembler routines that can be stored as an entity and made available to a high\-level language, REXX, or an assembler program\.

<a id="GLS changes"></a>

**changes**\. In installation and service, service supplied by IBM and original equipment manufacturers \(OEMs\) for their programs\. In the IBM service process, there are many ways users can receive information they need to fix \(change\) a portion\(s\) of a product they are running on a VM system\. These include PTFs, APARs, user modifications, and information received over the phone\. All these types of information are called *changes*\.

<a id="GLS checkpoint ( CKPT ) start"></a>

**checkpoint \(CKPT\) start**\. A VM/ESA system restart that attempts to recover information about closed spool files previously stored on the checkpoint cylinders\. The spool file chains are reconstructed, but the original sequence of spool files is lost\. Unlike warm start, CP accounting and system message information is also lost\. Contrast with *cold start, force start,* and *warm start*\.

<a id="GLS circumventive service"></a>

**circumventive service**\. Information that IBM supplies over the phone or on a tape to circumvent a problem by disabling a failing function until a PTF is available to be shipped as a corrective service fix\. See *patch* and *zap*\.

<a id="GLS CKD"></a>

**CKD**\. Count\-key\-data\.

<a id="GLS class A user"></a>

**class A user**\. See *primary system operator privilege class\.*

<a id="GLS class authority"></a>

**class authority**\. Privilege assigned to a virtual machine user in the user's directory entry; each class specified allows access to a subset of all the CP commands\. See *privilege class* and *user class restructure \(UCR\)*\.

<a id="GLS CMS"></a>

**CMS**\. Conversational Monitor System\.

<a id="GLS CMS EXEC"></a>

**CMS EXEC**\. An EXEC procedure or EDIT macro written in the CMS EXEC language and processed by the CMS EXEC processor\. Synonymous with *CMS program*\.

<a id="GLS CMS EXEC language"></a>

**CMS EXEC language**\. A general\-purpose, high\-level programming language, particularly suitable for EXEC procedures and EDIT macros\. The CMS EXEC processor executes procedures and macros \(programs\) written in this language\. Contrast with *EXEC 2 language* and *Restructured Extended Executor \(REXX\) language*\.

<a id="GLS CMS minidisk file directory"></a>

**CMS minidisk file directory**\. A directory on each CMS disk that contains the name, format, size, and location of each of the CMS files on that disk\. When a disk is accessed by the ACCESS command, its directory is read into virtual storage and identified with any letter from A through Z\. Synonymous with *master file directory block* and *minidisk directory*\.

<a id="GLS CMS nucleus"></a>

**CMS nucleus**\. The portion of CMS that is resident in the user's virtual storage whenever CMS is executing\. Each CMS user receives a copy of the CMS nucleus when the user IPLs CMS\. See *saved system* and *shared segment*\.

<a id="GLS CNTRL file"></a>

**CNTRL file**\. Control file with file type CNTRL\.

<a id="GLS cold start"></a>

**cold start**\. A VM/ESA system restart that ignores previous data areas and accounting information in main storage, and the contents of paging and spool files on CP\-owned disks\. Contrast with *checkpoint \(CKPT\) start, force start,* and *warm start*\.

<a id="GLS command"></a>

**command**\. A request from a user at a terminal for the execution of a particular CP, CMS, GCS, TSAF, Dump Viewing Facility, or AVS function\. A CMS command can also be the name of a CMS file with a file type of EXEC or MODULE\. See *subcommand* and *user\-written CMS command*\.

<a id="GLS command line"></a>

**command line**\. The line at the bottom of display panels that lets a user enter commands or panel selections\. It is prefixed by an arrow \(====\>\)\.

<a id="GLS commit"></a>

**commit**\. \(1\) In the context of SFS, to change a resource \(such as a file\) permanently\. \(2\) In the context of CRR, to make permanent changes to protected resources \(such as SFS file pools\) during a transaction \(CRR logical unit of work\)\. CRR commits changes made by an application program or transaction program\.

<a id="GLS COMMITTED"></a>

**COMMITTED**\. This status, listed in the receive status table, indicates that a PTF has been committed for the product\. This means that obsolete parts of the PTF may be discarded\.

<a id="GLS common storage"></a>

**common storage**\. A shared segment of reentrant code that contains free storage space, the GCS supervisor, control blocks, and data that all members of a virtual machine group share\.

<a id="GLS compile"></a>

**compile**\. To translate a program written in a high\-level programming language into a machine language program\.

<a id="GLS component"></a>

**component**\. A collection of objects that together form a separate functional unit\. A product may contain many components\. For example, CP, CMS, and TSAF are components of VM/ESA\.

<a id="GLS component override"></a>

**component override**\. Synonym for *component parameter override*\.

<a id="GLS component override area"></a>

**component override area**\. An area of the product parameter file or of a product parameter override file that contains one or more component parameter overrides\. Synonymous with *override area*\.

<a id="GLS component parameter override"></a>

**component parameter override**\. A component parameter, defined in a component override area, that updates or replaces a component parameter defined in a component area of the product parameter file\. Synonymous with *component override* and *override*\.

<a id="GLS concurrently"></a>

**concurrently**\. Concerning a mode of operation that includes doing work on two or more activities within a given \(short\) interval of time\.

<a id="GLS console"></a>

**console**\. A device used for communications between the operator or maintenance engineer and the computer\.

<a id="GLS console spooling"></a>

**console spooling**\. Synonym for *virtual console spooling*\.

<a id="GLS console stack"></a>

**console stack**\. Refers collectively to the program stack and the terminal input buffer\.

<a id="GLS control file"></a>

**control file**\. \(1\) In service, a file with file type CNTRL that contains records that identify the updates to be applied and the macro libraries, if any, needed to assemble that source program\. \(2\) A CMS file that is interpreted and directs the flow of a certain process through specific steps\. For example, the control file could contain installation steps, default addresses, and PTF prerequisite lists and many other necessary items\.

<a id="GLS control program"></a>

**control program**\. A computer program that schedules and supervises the program execution in a computer system\. See *Control Program \(CP\)*\.

<a id="GLS Control Program ( CP )"></a>

**Control Program \(CP\)**\. A component of VM/ESA that manages the resources of a single computer so multiple computing systems appear to exist\. Each of these apparent systems, or virtual machines, is the functional equivalent of an IBM System/370, 370\-XA, or ESA computer\. Also, XC virtual machines provide functions beyond the ESA architecture\. See also *virtual machine*\.

<a id="GLS control section ( CSECT )"></a>

**control section \(CSECT\)**\. The part of a program specified by the programmer to be a relocatable unit, all elements of which are loaded into adjoining main storage\.

<a id="GLS control statement"></a>

**control statement**\. A statement that controls or affects program execution in a data processing system\.

<a id="GLS copy file"></a>

**copy file**\. A file having file type COPY that contains nonexecutable real storage definitions that are referred to by macros and assemble files\.

<a id="GLS copy function"></a>

**copy function**\. The function initiated by a PF key to copy the contents of a display screen onto an associated hardcopy printer\. A remote display terminal copies the entire contents of the screen onto a printer attached to the same control unit\. A local display terminal copies all information from the screen, except the screen status information, onto any printer attached to any local display control unit\.

<a id="GLS COR"></a>

**COR**\. Corrective service tape\.

<a id="GLS corequisite"></a>

**corequisite**\. Corequisites identify other PTFs that must be applied at the same time this PTF is applied\. No specific order is required for applying corequisite PTFs\.

<a id="GLS corequisite change"></a>

**corequisite change**\. A change that must be applied to the user's product along with another change\. For example, if the user needs to apply change1 to the system and change1 has a corequisite of change2, then the user must apply both change1 and change2 to the system, but not in a specific order\. A corequisite change corrects a problem that requires changes to one or more elements of a product or component\.

<a id="GLS corrective service"></a>

**corrective service**\. Service that IBM supplies on tape to correct a specific problem\.

<a id="GLS corrective service tape"></a>

**corrective service tape**\. A tape, supplied by IBM at the user's request, containing a fix for a specific problem and any requisites for the fix\.

<a id="GLS count - key - data ( CKD ) device"></a>

**count\-key\-data \(CKD\) device**\. A DASD that stores data in the format: count field, usually followed by a key field, followed by the actual data of a record\. The count field contains the cylinder number, head number, record number, and the length of the data\. The key field contains the record's key \(search argument\)\.

<a id="GLS CP"></a>

**CP**\. Control Program\.

<a id="GLS CP command"></a>

**CP command**\. A command available to all VM users\. Class G CP commands let the general user reconfigure their virtual machine, control devices attached to their virtual machine, do input and output spooling functions, and simulate many other functions of a real computer console\. Other CP commands let system operators, system programmers, system analysts, and service representatives manage the resources of the system\.

<a id="GLS CP directory"></a>

**CP directory**\. Synonym for *VM directory*\.

<a id="GLS CP read"></a>

**CP read**\. The condition when CP is waiting for a response or request for work from the user\. On a typewriter terminal, the keyboard is unlocked; on a display terminal, the screen status area indicates CP READ\.

<a id="GLS cross system extensions ( CSE )"></a>

**cross system extensions \(CSE\)**\. An environment in which end users attached to a single system can participate with additional systems as though all participating systems were one complex\.

<a id="GLS CSE"></a>

**CSE**\. Cross system extensions\.

<a id="GLS CSECT"></a>

**CSECT**\. Control section\.

<a id="GLS CSL"></a>

**CSL**\. Callable services library\.

<a id="GLS cylinder"></a>

**cylinder**\. In a disk pack, the set of all tracks with the same nominal distance from the axis about which the disk pack rotates\.

<a id="GLS"></a>

```
       ___
      | D |
      |___|
```

<a id="GLS DASD"></a>

**DASD**\. Direct access storage device\.

<a id="GLS DASD Dump Restore ( DDR ) program"></a>

**DASD Dump Restore \(DDR\) program**\. A service program that copies all or part of a minidisk onto tape, loads the contents of a tape onto a minidisk, or sends data from a DASD or from tape to the virtual printer\.

<a id="GLS DASD space"></a>

**DASD space**\. \(1\) Area allocated to DASD units on CKD devices\. \(2\) Area allocated to DASD units on FB\-512 devices\. Note that *DASD space* is synonymous with *cylinder* when there is no need to differentiate between CKD devices and FB\-512 devices\.

<a id="GLS DBCS"></a>

**DBCS**\. Double\-byte character set\.

<a id="GLS DCSS"></a>

**DCSS**\. Discontiguous saved segment\.

<a id="GLS DDR program"></a>

**DDR program**\. DASD Dump Restore program\.

<a id="GLS DELETE"></a>

**DELETE**\. This status, shown in the service\-level build status table, indicates the object has been removed from the build list, and the corresponding object must be deleted\.

<a id="GLS DELETED"></a>

**DELETED**\. This status, listed in the apply status table, indicates that a product has been deleted from the system\. In the service\-level build status table, it indicates that an object has been deleted from the product\.

<a id="GLS delimiter"></a>

**delimiter**\. \(1\) A flag that separates and organizes items of data\. Synonymous with *separator*\. \(2\) A character that groups or separates words or values in a line of input\. Usually one or more blank characters separate the command name and each operand or option in the command line\. In certain cases, a tab, left parenthesis, or backspace character can also act as a delimiter\.

<a id="GLS Delta disk"></a>

**Delta disk**\. In VMSES/E, a minidisk or SFS directory containing a list of the files on a PTF\. See program temporary fix \(PTF\)\.

<a id="GLS Delta string"></a>

**Delta string**\. In VMSES/E, the set of Delta disks\.

<a id="GLS dependent PTF"></a>

**dependent PTF**\. A PTF that has another PTF as a prerequisite or corequisite\.

<a id="GLS dependent requisite"></a>

**dependent requisite**\. A dependent requisite is a product that must be installed before another product can be installed correctly\. Unlike pre\-requisites, dependent requisites are no longer satisfied when the requisite product is superseded\. This occurs when a product requires a specific level of another product and newer levels of the product will not meet the requirements\.

<a id="GLS description table"></a>

**description table**\. The Software Inventory table that contains the descriptive name for a product, in the system level, and APARs in the service level\. The file type of the system level inventory table is SYSDESCT and the file type of the service level inventory table is SRVDESCT\.

<a id="GLS device support facilities"></a>

**device support facilities**\. A program for doing operations on disk volumes so that they can be accessed by IBM and user programs\. Examples of these operations are initializing a disk volume and assigning an alternate track\.

<a id="GLS DIRCONTROL directory"></a>

**DIRCONTROL directory**\. Synonym for *directory control directory*\.

<a id="GLS direct access storage device ( DASD )"></a>

**direct access storage device \(DASD\)**\. A storage device in which the access time is effectively independent of the location of the data\.

<a id="GLS directory"></a>

**directory**\. See *auxiliary directory, CMS minidisk file directory DIRCONTROL directory, directory control directory, file control directory, FILECONTROL directory, SFS directory,* or *VM directory*\.

<a id="GLS directory identifier ( dirid )"></a>

**directory identifier \(dirid\)**\. A fully\-qualified directory name \(in which the file pool ID and user ID can be allowed to default\), a file mode letter, or plus \(\+\) or minus \(\-\) file mode syntax \(used in commands\)\.

<a id="GLS directory name ( dirname )"></a>

**directory name \(dirname\)**\. A fully\-qualified directory name that can incorporate a period \(\.\) to indicate the user's own top directory \(used in commands\)\.

<a id="GLS dirid"></a>

**dirid**\. Directory identifier\.

<a id="GLS dirname"></a>

**dirname**\. Directory name\.

<a id="GLS discontiguous saved segment"></a>

**discontiguous saved segment**\. One or more segments of storage that were previously loaded, saved, and assigned a unique name\. In VM/ESA, a segment begins and ends on a 1MB boundary\. The segment can be shared among virtual machines if the segment contains reentrant code\. Discontiguous segments used with CMS must be loaded into storage at locations above the address space of a user's CMS virtual machine\. They can be detached when no longer needed\.

<a id="GLS disk"></a>

**disk**\. A magnetic disk unit in the user's CMS virtual machine configuration\. See *virtual disk*\.

<a id="GLS display device"></a>

**display device**\. An I/O device that gives a visual representation of data\.

<a id="GLS display terminal"></a>

**display terminal**\. A terminal with a component that can display information on a viewing surface such as a screen or gas panel\.

<a id="GLS distributed function terminal ( DFT )"></a>

**distributed function terminal \(DFT\)**\. An operational mode that allows multiple concurrent logical terminal sessions\. Contrast with *control unit terminal \(CUT\)*\.

<a id="GLS DMSPARMS file"></a>

**DMSPARMS file**\. A CMS file with a file type of DMSPARMS that contains the start\-up parameters that SFS file pool server and CRR recovery server processing uses\.

<a id="GLS double - byte character set ( DBCS )"></a>

**double\-byte character set \(DBCS\)**\. A character set that requires 2 bytes to uniquely define each character\. This contrasts with EBCDIC, in which each printed character is represented by 1 byte\.

<a id="GLS dump"></a>

**dump**\. To write the contents of part or all of main storage, or part or all of a minidisk, to auxiliary storage or a printer\. See *abend dump*\.

<a id="GLS"></a>

```
       ___
      | E |
      |___|
```

<a id="GLS ECKD"></a>

**ECKD**\. Extended count\-key data\.

<a id="GLS edit"></a>

**edit**\. A function that makes changes, additions, or deletions to a file on a disk\. These changes are interactively made\. The edit function also generates information in a file that did not previously exist\.

<a id="GLS ERROR"></a>

**ERROR**\. This qualifier of the status field in the service\-level build status table indicates that an error was encountered when building an object\. In the system\-level build status table, it indicates that an error was detected when building a product or object\.

<a id="GLS ESA virtual machine"></a>

**ESA virtual machine**\. A virtual machine that simulates ESA/370 or ESA/390 functions\. Contrast with *370 virtual machine*, *XA virtual machine*, and *XC virtual machine*

<a id="GLS exclude list"></a>

**exclude list**\. A file listing PTFs to be omitted from a product or component\.

<a id="GLS EXEC procedure"></a>

**EXEC procedure**\. \(1\) A procedure defined by a frequently used sequence of CMS and CP commands to do a commonly required function\. A user creates the procedure to save repetitious reentering of the sequence, and invokes the entire procedure by entering a command \(that is, the exec file's file name\)\. The procedure could consist of a long sequence of CMS and CP commands, along with REXX, EXEC 2, or CMS EXEC control statements to control processing within the procedure\. \(2\) A CMS file with a file type of EXEC\.

<a id="GLS EXEC 2 language"></a>

**EXEC 2 language**\. A general\-purpose, high\-level programming language, particularly suitable for EXEC procedures and XEDIT macros\. The EXEC 2 processor runs procedures and XEDIT macros \(programs\) written in this language\. Contrast with *CMS EXEC language* and *Restructured Extended Executor \(REXX\) language*\.

<a id="GLS exit"></a>

**exit**\. See *user exit* and *installation\-wide exit*\.

<a id="GLS"></a>

```
       ___
      | F |
      |___|
```

<a id="GLS FB - 512"></a>

**FB\-512**\. An FBA device that stores data in 512\-byte blocks \(refers to DASD devices such as the IBM 9335, 9332, 9313, 3370, and 3310\)\.

<a id="GLS FBA"></a>

**FBA**\. Fixed\-block architecture\.

<a id="GLS feature"></a>

**feature**\. A feature is associated with the software distribution order number which has a type, model, and feature field\. The feature field identifies a particular deliverable for the given product offering\.

<a id="GLS file access mode"></a>

**file access mode**\. A file mode number that designates whether the file can be used as a read\-only or read/write file by a user\. See *file mode*\.

<a id="GLS file ID"></a>

**file ID**\. A CMS file identifier that consists of a file name, file type, file mode, or directory ID\. The file ID is associated with a particular file when the file is created, defined, or renamed under CMS\. See *file name, file type,* and *file mode*\.

<a id="GLS file mode"></a>

**file mode**\. A two\-character CMS file identifier field containing the file mode letter \(A through Z\) followed by the file mode number \(0 through 6\)\. The file mode letter indicates the minidisk or SFS directory on which the file resides\. The file mode number indicates the access mode of the file\. See *file access mode*\.

<a id="GLS file name"></a>

**file name**\. A one\-to\-eight character alphanumeric field, containing A through Z, 0 through 9, and special characters $ \# @ \+ \- \(hyphen\) : \(colon\) \_ \(underscore\), that is part of the CMS file identifier and serves to identify the file for the user\.

<a id="GLS file pool"></a>

**file pool**\. A collection of minidisks managed by SFS\. It contains user files and directories and associated control information\. Many users' files and directories can be contained in a single file pool\.

<a id="GLS file type"></a>

**file type**\. A one\-to\-eight character alphanumeric field, containing A through Z, 0 through 9, and special characters $ \# @ \+ \- \(hyphen\) : \(colon\) \_ \(underscore\), that is used as a descriptor or as a qualifier of the file name field in the CMS file identifier\. See *reserved file types*\.

<a id="GLS file type abbreviation ( ftabbrev )"></a>

**file type abbreviation \(ftabbrev\)**\. The 3\-character PTF abbreviation or the real CMS file type for a part that is not serviced by replacement\.

<a id="GLS file type abbreviation table"></a>

**file type abbreviation table**\. The Software Inventory table that identifies the mapping between PTF\-numbered file types and the real CMS file type\. The service level inventory does not contain this table\.

<a id="GLS fixed - block architecture ( FBA ) device"></a>

**fixed\-block architecture \(FBA\) device**\. A disk storage device that stores data in blocks of fixed size or records; these blocks are addressed by block number relative to the beginning of the particular file\.

<a id="GLS flat file"></a>

**flat file**\. A file that consists of a set of records ordered by record number or as sequentially entered in the file; a two dimensional file\.

<a id="GLS free storage"></a>

**free storage**\. Storage not allocated\. The blocks of central storage available for temporary use by programs or by the system\.

<a id="GLS ftabbrev"></a>

**ftabbrev**\. File type abbreviation

<a id="GLS full - pack minidisk"></a>

**full\-pack minidisk**\. A virtual disk that contains all of the addressable cylinders of a real DASD volume\.

<a id="GLS"></a>

```
       ___
      | G |
      |___|
```

<a id="GLS GCS"></a>

**GCS**\. Group Control System for ESA/370 or ESA/390 architecture\.

<a id="GLS group"></a>

**group**\. Synonym for *virtual machine group*\.

<a id="GLS Group Control System ( GCS )"></a>

**Group Control System \(GCS\)**\. A component of VM/ESA, consisting of a shared segment that the user can IPL and run in a virtual machine\. It provides simulated MVS services and unique supervisor services to help support a native SNA network\.

<a id="GLS GROUP EXEC"></a>

**GROUP EXEC**\. A GCS installation tool that prompts you for the specifications needed to build a GCS configuration file\.

<a id="GLS guest"></a>

**guest**\. An operating system running in a virtual machine managed by a VM control program\. Contrast with *host*\.

<a id="GLS"></a>

```
       ___
      | H |
      |___|
```

<a id="GLS hard requisites"></a>

**hard requisites**\. The hard requisites of a PTF are a subset of its prerequisites\. There are two reasons for a prerequisite to be classified as a hard requisite\. First, if the PTF depends on a functional change introduced by the requisite, the requisite is considered a hard requisite\. For example, the requisite introduces a new flag and the PTF exploits it\. Second, if any of the updates in the PTF affect the same lines of code as the requisite, such that the new update will not apply without the older one, then it is a hard requisite relationship\. \(Corequisites and if\-requisites are by definition hard requisites and are not explicitly listed as hard requisites\)\.

<a id="GLS history files"></a>

**history files**\. One or more CMS files that describe the changes \(with a date and time stamp\) made to the VM/ESA system and its installed software products\.

<a id="GLS"></a>

```
       ___
      | I |
      |___|
```

<a id="GLS I / O"></a>

**I/O**\. Input/output\.

<a id="GLS if - requisite"></a>

**if\-requisite**\. \(1\) At the system\-level, an if\-requisite lists two products\. The first one becomes a requisite product if and only if the second one is installed\. \(2\) At the service\-level, an if\-requisite lists a PTF in another product that must be applied if and only if the other product is installed\.

<a id="GLS image library"></a>

**image library**\. A set of modules that define the spacing, characters, and copy modification data that a 3800 printer uses to print a spool file or that define the spacing and character set that an impact printer uses to print a spool file\. See *system data file*\.

<a id="GLS initial installation system"></a>

**initial installation system**\. In VMSES/E, a functional subset of the VM/ESA system shipped on the VM/ESA system DDR tapes and used during installation of VM/ESA\.

<a id="GLS initial program load ( IPL )"></a>

**initial program load \(IPL\)**\. The initialization procedure that causes an operating system to begin operation\. A VM user must IPL the specific operating system into the virtual machine that will control the user's work\. Each virtual machine can be loaded with a different operating system\.

<a id="GLS initialize"></a>

**initialize**\. To set counters, switches, addresses, or contents of storage to starting values\.

<a id="GLS input / output ( I / O )"></a>

**input/output \(I/O\)**\. \(1\) A device whose parts can do an input process and an output process at the same time\. \(2\) A functional unit or channel involved in an input process, output process, or both, concurrently or not, and to the data involved in such a process\.

<a id="GLS installation - wide exit"></a>

**installation\-wide exit**\. An interface to VM/ESA that a system programmer can use to enhance or extend the functions of a VM/ESA system\. Generally, an installation\-wide exit is activated for all users on the system and is run as part of a system program\.

<a id="GLS install - time requisites"></a>

**install\-time requisites**\. Product\(s\) that must be installed before this product can be installed correctly\.

<a id="GLS interactive"></a>

**interactive**\. The classification given to a virtual machine depending on this virtual machine's processing characteristics\. When a virtual machine uses less than its allocation time slice because of terminal I/O, the virtual machine is classified as being interactive\. Contrast with *noninteractive*\.

<a id="GLS interface"></a>

**interface**\. A shared boundary between two or more entities\. An interface might be a hardware or software component that links two devices or programs together\.

<a id="GLS interrupt"></a>

**interrupt**\. A suspension of a process, such as execution of a computer program, caused by an external event and done in such a way that the process can be resumed\.

<a id="GLS invoke"></a>

**invoke**\. To start a command, procedure, or program\.

<a id="GLS IPL"></a>

**IPL**\. Initial program load\.

<a id="GLS"></a>

```
       ___
      | L |
      |___|
```

<a id="GLS line number"></a>

**line number**\. A number located at either the beginning or the end of a record \(line\) that can be used during editing to refer to that line\. See *prompting*\.

<a id="GLS load"></a>

**load**\. In installation and service, to move files from tape to disk, auxiliary storage to main storage, or minidisks to virtual storage within a virtual machine\.

<a id="GLS load map"></a>

**load map**\. A map containing the storage addresses of control sections and entry points of a program loaded into storage\.

<a id="GLS loadable unit"></a>

**loadable unit**\. A portion of a product that can be installed independently of the rest of the product, but is serviced as part of the product\.

<a id="GLS loader"></a>

**loader**\. A routine, commonly a computer program, that reads data into main storage\.

<a id="GLS Local disk"></a>

**Local disk**\. In VMSES/E, a minidisk or SFS directory containing local modifications, customized files, and any circumventive service\.

<a id="GLS local modification"></a>

**local modification**\. Any change applied to a product other than a PTF See circumventive service and user modification\.

<a id="GLS local service"></a>

**local service**\. Changes manually applied to a product or component \(that is, not using the program update service or corrective service procedures\)\. See *circumventive service* and *user modification*\.

<a id="GLS local tracking number"></a>

**local tracking number**\. The unique identifier assigned to a local modification\. The local tracking number is used in the file type of update files and in the update file identification records of auxiliary control files\. Each installation has its own system of local tracking numbers\.

<a id="GLS Local string"></a>

**Local string**\. In VMSES/E, the set of Local disks\.

<a id="GLS logical record"></a>

**logical record**\. A formatted record that consists of a 2\-byte logical record length and a data field of variable length\.

<a id="GLS logical saved segment"></a>

**logical saved segment**\. A portion of a physical saved segment that CMS can manipulate\. Each logical segment can contain different types of program objects, such as modules, text files, execs, callable services libraries, language repositories, user\-defined objects, or a single minidisk directory\. A system segment identification file \(SYSTEM SEGID\) associates a logical saved segment to the physical saved segment in which it resides\. See *physical saved segment* and *saved segment*\.

<a id="GLS logoff"></a>

**logoff**\. The procedure by which a user ends a terminal session\.

<a id="GLS logon"></a>

**logon**\. The procedure by which a user begins a terminal session\.

<a id="GLS low common storage"></a>

**low common storage**\. GCS common storage that resides below the 16MB line\. See *common storage*\.

<a id="GLS"></a>

```
       ___
      | M |
      |___|
```

<a id="GLS machine"></a>

**machine**\. A synonym for a virtual machine running under the control of VM/ESA\.

<a id="GLS macro"></a>

**macro**\. Synonym for *macrodefinition* and *macroinstruction*\.

<a id="GLS macro library"></a>

**macro library**\. A library of macrodefinitions\.

<a id="GLS macrodefinition"></a>

**macrodefinition**\. A set of statements that defines the name of, format of, and conditions for generating a sequence of assembler language statements from a single source statement\. Synonymous with *macro*\.

<a id="GLS macroinstruction"></a>

**macroinstruction**\. In assembler language programming, an assembler language statement that causes the assembler to process a predefined set of statements called a macrodefinition\. The statements usually produced from the macrodefinition replace the macroinstruction in the program\. Synonymous with *macro*\.

<a id="GLS MANUAL"></a>

**MANUAL**\. This status, listed in the service\-level build status table, indicates that the object requires MANUAL processing\.

<a id="GLS map"></a>

**map**\. In CMS, the file that contains a CMS output listing, such as \(1\) a list of macros in the MACLIB library, including macro size and location within the library; \(2\) a listing of the directory entries for the DOS/VS system or private source, relocatable, or core image libraries; \(3\) a linkage editor map for CMS/DOS programs; and \(4\) a module map containing entry point locations\.

<a id="GLS mapping"></a>

**mapping**\. To show relationships between objects\.

<a id="GLS MB"></a>

**MB**\. Megabyte\.

<a id="GLS MDISK"></a>

**MDISK**\. \(1\) Another name for minidisk\. \(2\) The VM directory statement that describes a user's storage space\.

<a id="GLS megabyte ( MB )"></a>

**megabyte \(MB\)**\. 1,048,576 bytes\.

<a id="GLS member saved segment"></a>

**member saved segment**\. A saved segment that begins and ends on a page boundary\. It can be a member in up to 64 segment spaces and is accessed either by the segment space name or by its own name\. Contrast with *discontiguous saved segment*\. See *saved segment*, *segment*, and *segment space*\.

<a id="GLS memo - to - users"></a>

**memo\-to\-users**\. A file provided on a service tape that contains specific service information for a product\.

<a id="GLS merge"></a>

**merge**\. When receiving files from a service tape using VMFMRDSK, the process of moving existing service files from each minidisk or SFS directory in the target string to the minidisk or directory that contains the previous service level\. The result is that the primary target minidisk or directory is left empty and ready to receive the latest service\.

<a id="GLS message"></a>

**message**\. Data sent from a source application to a target application program in a conversation\. See also *message text*, *message key*, and *message header*\.

<a id="GLS minidisk"></a>

**minidisk**\. A logical subdivision \(or all\) of a physical disk pack that has its own virtual device address, consecutive virtual cylinders \(starting with virtual cylinder 0\), and a VTOC or disk label identifier\. Each user virtual disk is preallocated and defined by a VM/ESA directory entry as belonging to a user\.

<a id="GLS minidisk directory"></a>

**minidisk directory**\. Synonym for *CMS minidisk file directory*\.

<a id="GLS module"></a>

**module**\. \(1\) A unit of a software product that is discretely and separately identifiable with respect to modifying, compiling, and merging with other units, or with respect to loading and execution\. For example, the input to, or output from, a compiler, the assembler, the linkage editor, or an exec routine\. \(2\) A nonrelocatable file whose external references have been resolved\.

<a id="GLS"></a>

```
       ___
      | N |
      |___|
```

<a id="GLS named saved system ( NSS )"></a>

**named saved system \(NSS\)**\. A copy of an operating system that a user has named and saved in a file\. The user can load the operating system by its name, which is more efficient than loading it by device number\. See *discontiguous saved segment*, *member saved segment*, *saved segment*, *segment space*, and *system data file*\.

<a id="GLS negative prerequisite"></a>

**negative prerequisite**\. In VMSES/E, a product that cannot exist on a system at the same time as another product\.

<a id="GLS NSS"></a>

**NSS**\. Named saved system\.

<a id="GLS nucleus"></a>

**nucleus**\. The part of CP and CMS resident in main storage\.

<a id="GLS"></a>

```
       ___
      | O |
      |___|
```

<a id="GLS object"></a>

**object**\. In VMSES/E, a usable form defined in build lists\. A built part of a product\. A product consists of many objects, for example, nuclei, modules, execs, help files, and macro libraries\. See usable forms\.

Compare *subject*\.

<a id="GLS object code"></a>

**object code**\. Compiler or assembler output that is executable machine code or is suitable for more processing to produce executable machine code\. Contrast with *source code*\.

<a id="GLS object module"></a>

**object module**\. A module that is the output of an assembler or a compiler and is input to a linkage editor\.

<a id="GLS operand"></a>

**operand**\. Information entered with a command name to define the data on which a command processor operates and to control the execution of the command processor\.

<a id="GLS out - of - component requisite"></a>

**out\-of\-component requisite**\. In VMSES/E, at the service\-level, a PTF from another product that must be applied to that product in order for this PTF to function properly\.

<a id="GLS overhead"></a>

**overhead**\. The additional processor time charged to each virtual machine for the CP functions needed to simulate the virtual machine environment and for paging and scheduling time\.

<a id="GLS override"></a>

**override**\. Synonym for *component parameter override*\.

<a id="GLS override area"></a>

**override area**\. Synonym for *component override area*\.

<a id="GLS override file"></a>

**override file**\. Synonym for *class override file* and *product parameter override file*\.

<a id="GLS override $ PPF"></a>

**override $PPF**\. Synonym for *override product parameter file*\.

<a id="GLS"></a>

```
       ___
      | P |
      |___|
```

<a id="GLS pack"></a>

**pack**\. A set of flat, circular recording surfaces that a disk storage device uses\. A disk pack\.

<a id="GLS page"></a>

**page**\. A fixed\-length block that has a virtual address and can be transferred between real storage and auxiliary storage\.

<a id="GLS parameter"></a>

**parameter**\. A variable that is given a constant value for a specified application and that may denote the application\.

<a id="GLS parameter driven installation ( PDI )"></a>

**parameter driven installation \(PDI\)**\. A product format that lets you specify a product installation location, specify installation related parameters, install multiple copies of a product, and select a default installation path\.

<a id="GLS part"></a>

**part**\. A CMS file provided on a product tape or service tape as input to the build process\. See *build*\. A part is the smallest serviceable unit of a component\.

<a id="GLS part handler"></a>

**part handler**\. An exec provided by VMSES/E that builds a specific type of object or loads parts from service media\.

<a id="GLS parts catalog"></a>

**parts catalog**\. In VMSES/E, a set of Software Inventory files that catalog all parts of a product on a minidisk or SFS directory\. All product parts are cataloged when they are loaded onto the system, when they are generated, and when they are moved\.

<a id="GLS password"></a>

**password**\. In computer security, a string of characters known to the computer system and a user, who must specify it to gain full or limited access to a system and to the data stored within it\.

<a id="GLS patch"></a>

**patch**\. A circumventive service change applied directly to object code in a text deck in a nucleus\.

<a id="GLS patch update file"></a>

**patch update file**\. A file containing a single patch\. The file can also specify requisites for applying the patch\.

<a id="GLS PDI"></a>

**PDI**\. Parameter driven installation\.

<a id="GLS PF key"></a>

**PF key**\. Programmed function key\.

<a id="GLS physical saved segment"></a>

**physical saved segment**\. One or more pages of storage that have been named and retained on a CP\-owned volume \(DASD\)\. Once created, it can be loaded within a virtual machine's address space or outside a virtual machine's address space\. Multiple users can load the same copy\. A physical saved segment can contain one or more logical saved segments\. A system segment identification file \(SYSTEM SEGID\) associates a physical saved segment to its logical saved segments\. See *logical saved segment* and *saved segment*\.

<a id="GLS PPF"></a>

**PPF**\. Product parameter file\.

<a id="GLS preferred auxiliary file"></a>

**preferred auxiliary file**\. In CMS, an auxiliary file that applies to a particular version of a source module to be updated, if multiple versions of the module exist\.

<a id="GLS preferred virtual machine"></a>

**preferred virtual machine**\. A particular virtual machine that has one or more of the performance options assigned to it\.

<a id="GLS prefix area"></a>

**prefix area**\. The five left\-most positions on the XEDIT full\-screen display, in which prefix subcommands or prefix macros can be entered\. See *prefix macros* and *prefix subcommands*\.

<a id="GLS prefix macros"></a>

**prefix macros**\. XEDIT macros entered in the prefix area of any line on a full\-screen display\. See *prefix area*\.

<a id="GLS prefix subcommands"></a>

**prefix subcommands**\. XEDIT subcommands entered in the prefix area of any line on a full\-screen display\. See *prefix area*\.

<a id="GLS prerequisite"></a>

**prerequisite**\. In VMSES/E, at the system\-level, a product that must be installed before another product can be installed\. At the service\-level, a PTF that must be installed before another product can be installed\.

<a id="GLS prerequisite change"></a>

**prerequisite change**\. A change that must be applied to the system before another change can be applied\. For example, change2 lists change1 as a prerequisite\. This indicates that the user must apply change1 before applying change2\.

<a id="GLS preventive service"></a>

**preventive service**\. The application of all PTFs from a PUT or RSU\. Contrast with selective preventive service\. See program update tape and product service upgrade\.

<a id="GLS primary system operator privilege class"></a>

**primary system operator privilege class**\. The CP privilege class A user\. This operator has primary control over the VM/ESA system and can enable and disable teleprocessing lines, lock and unlock pages, force users off the VM/ESA system, issue warning messages, query, and set \(and reset\) performance options for selected virtual machines, and invoke VM/ESA accounting\. If the current primary system operator logs off, the next class A user to log on becomes the primary system operator\.

<a id="GLS private storage"></a>

**private storage**\. A combination of application code and GCS code available to only one particular virtual machine\. No virtual machine can access or share another's private storage area\.

<a id="GLS privilege class"></a>

**privilege class**\. One or more classes assigned to a virtual machine user in a VM/ESA directory entry; each privilege class specified lets a user access a logical subset of the CP commands\. There are nine IBM\-defined privilege classes that correspond to specific administrative functions\. They are:

Class A \- primary system operator Class B \- system resource operator Class C \- system programmer Class D \- spooling operator Class E \- system analyst Class F \- service representative Class G \- general user Class H \- reserved for IBM use Class Any \- available to any user\.

The privilege classes can be changed to meet the needs of an installation\. See *class authority* and *user class restructure \(UCR\)*\.

<a id="GLS privileged program"></a>

**privileged program**\. In GCS, a program called by a GCS application that operates in supervisor state and uses privileged functions\. A privileged program is one that meets either of the following requirements:

- It runs in an authorized virtual machine\.
- It is called through the AUTHCALL facility\.

Synonymous with *authorized program*\. Contrast with *nonprivileged program*\.

<a id="GLS process"></a>

**process**\. \(1\) A systematic sequence of operations to produce a specified result\. A process is usually logical, not physical\. \(2\) In CMS Multitasking, a collection of threads performing related work\. A process can have resources associated with it, such as storage subpools, queues, open files, and APPC conversations\. All threads in a process have equal access to the resources associated with the process\.

<a id="GLS PRODPART file"></a>

**PRODPART file**\. VMSES/E uses information in this file, included on a product's install tape, to update entries in the system\-level Software Inventory each time a product is loaded onto your system\.

<a id="GLS product"></a>

**product**\. Any separately installable software program, whether supplied by IBM or otherwise, distinct from others and recognizable by a unique identification code\. The product identification code is unique to a given product, but does not identify the release level of that product\.

<a id="GLS product identifier ( prodid )"></a>

**product identifier \(prodid\)**\. The product identifier is the 7\- or 8\-alphanumeric character identifier assigned to the product by IBM\.

<a id="GLS product parameter file ( PPF )"></a>

**product parameter file \(PPF\)**\. A file containing installation and service parameters for a product: control options, minidisk and SFS directory assignments, and component part type/function lists\.

<a id="GLS product parameter override file"></a>

**product parameter override file**\. A file containing one or more component override areas\.

<a id="GLS product processing exit"></a>

**product processing exit**\. An interface used by program products to perform additional product installation tasks\.

<a id="GLS product service upgrade ( PSU )"></a>

**product service upgrade \(PSU\)**\. A procedure used to upgrade the service level of a product or component using a recommended service upgrade \(RSU\) tape\.

<a id="GLS product tape"></a>

**product tape**\. One of a set of tapes containing individual components or products to load and build\.

<a id="GLS PROFILE EXEC"></a>

**PROFILE EXEC**\. A special EXEC procedure with a file name of PROFILE that a user can create\. The procedure is usually executed immediately after CMS is loaded into a virtual machine \(also known as IPL CMS\)\.

<a id="GLS program temporary fix ( PTF )"></a>

**program temporary fix \(PTF\)**\. Code changes needed to correct a problem reported in an APAR\. The corrected code is included in later releases\. A PTF contains one or more APAR fixes\. For object\-maintained parts that are changed, the PTF includes replacement parts\. For source\-maintained parts that are changed, the PTF includes update files and replacement parts\. Each PTF is unique to a given release of a product\. If the same problem occurs in multiple releases of a product, a separate PTF is defined for each release\.

<a id="GLS program update service"></a>

**program update service**\. Receiving service from a PUT or RSU, applying all or some of the changes, and rebuilding the serviced parts\. See preventive service and selective preventive service\.

<a id="GLS program update tape ( PUT )"></a>

**program update tape \(PUT\)**\. A tape containing a customized collection of service tapes \(preventive service\) to match the products listed in a customer's ISD \(IBM Software Distribution\) profile\. Each PUT contains cumulative service for the customer's products back to earlier release levels of the product still supported\. The tape is distributed to authorized customers of the products at scheduled intervals or on request\.

<a id="GLS programmed function ( PF ) key"></a>

**programmed function \(PF\) key**\. On a terminal, a key that can do various functions selected by the user or determined by an application program\.

<a id="GLS prompt"></a>

**prompt**\. A displayed message that describes required input or gives operational information\.

<a id="GLS prompting"></a>

**prompting**\. An interactive technique that lets the program guide the user in supplying information to a program\. The program types or displays a request, question, message, or number, and the user enters the desired response\. The process is repeated until all the necessary information is supplied\.

<a id="GLS PSU"></a>

**PSU**\. product service upgrade

<a id="GLS PTF"></a>

**PTF**\. Program temporary fix\.

<a id="GLS PTF number"></a>

**PTF number**\. A number assigned by service organizations that uniquely identifies a PTF; for example, IBM uses UVNNNNN for a VM\-unique product, and UPnnnnn for a cross\-system product\. PTFs for different products or different releases of a product have different numbers\.

<a id="GLS PUT"></a>

**PUT**\. Program update tape\.

<a id="GLS"></a>

```
       ___
      | R |
      |___|
```

<a id="GLS R / O"></a>

**R/O**\. Read\-only\.

<a id="GLS R / W"></a>

**R/W**\. Read/write\.

<a id="GLS rdev"></a>

**rdev**\. The real device address of an I/O device\.

<a id="GLS reach - ahead service"></a>

**reach\-ahead service**\. Corrective service or local service that has been applied to a product but is not available on a program update tape, product service upgrade, or other service vehicle\.

<a id="GLS read authority"></a>

**read authority**\. The authority to read the contents of a file without being able to change them\. For a directory, read authority lets the user view the names of the objects in the directory\.

<a id="GLS read - only access"></a>

**read\-only access**\. An access mode associated with a virtual disk or SFS directory that lets a user read, but not write or update, any file on the disk or SFS directory\.

<a id="GLS read / write access"></a>

**read/write access**\. An access mode associated with a virtual disk or SFS directory that lets a user read and write any file on the disk or SFS directory \(if write authorized\)\.

<a id="GLS real address"></a>

**real address**\. The address of a location in real storage or the address of a real I/O device\.

<a id="GLS receive"></a>

**receive**\. \(1\) Bringing into the specified buffer data sent to the user's virtual machine from another virtual machine or from the user's own virtual machine\. \(2\) To load service files from a service tape\. \(3\) In CMS Multitasking interprocess communication, the action of retrieving a message from a queue\.

<a id="GLS receive ID"></a>

**receive ID**\. A 7\- or 8\- alphanumeric character identifier that is used to name the Software Inventory files created during receive processing\.

<a id="GLS receive status table"></a>

**receive status table**\. The Software Inventory table that contains the relationship between a product and the $PPF file used to install it\. It also identifies what products of PTFs have been received or committed\. The file type of the system level inventory table is SYSRECS and the file type of the service level inventory table is SRVRECS\.

<a id="GLS RECEIVED"></a>

**RECEIVED**\. This status, listed in the receive status table, indicates that a product or PTF has been RECEIVED on the system\.

<a id="GLS Recommended Service Upgrade ( RSU ) tape"></a>

**Recommended Service Upgrade \(RSU\) tape**\. A tape containing preventive service for upgrading the current release of a VM/ESA system once it has been installed\.

<a id="GLS recomp"></a>

**recomp**\. To change the number of cylinders or blocks on the disk that are available to you\.

<a id="GLS regression"></a>

**regression**\. Causing serviced parts to go back to earlier levels\. This can occur when applying changes from a PUT to parts updated by corrective service or user modifications\.

<a id="GLS Remote Spooling Communications Subsystem Networking ( RSCS )"></a>

**Remote Spooling Communications Subsystem Networking \(RSCS\)**\. An IBM licensed program and special\-purpose subsystem that supports the reception and transmission of messages, files, commands, and jobs over a computer network\.

<a id="GLS REMOVED"></a>

**REMOVED**\. This status, listed in the apply status table, indicates that the PTF has been REMOVED from the system\.

<a id="GLS replacement parts"></a>

**replacement parts**\. See serviceable parts\.

<a id="GLS replacement service"></a>

**replacement service**\. Servicing a part by replacing the part with a new one\.

<a id="GLS requisite"></a>

**requisite**\. The requirements of a product or PTF\.

<a id="GLS requisite relationships"></a>

**requisite relationships**\. The interrelated requirements of a product or PTF\.

<a id="GLS requisite table"></a>

**requisite table**\. The Software Inventory table that contains the requisite relationships between products, in the system level, and PTFs in the service level\. The file type of the system level inventory table is SYSREQT and the file type of the service level inventory table is SRVREQT\.

<a id="GLS resource"></a>

**resource**\. A program, a data file, a specific set of files, a device, or any other entity or a set of entities that the user can uniquely identify for application program processing in a VM system\.

<a id="GLS REXX exec"></a>

**REXX exec**\. An EXEC procedure or XEDIT macro written in the REXX language and processed by the REXX/VM Interpreter\. Synonymous with *REXX program*\.

<a id="GLS REXX program"></a>

**REXX program**\. Synonym for *REXX exec*\.

<a id="GLS RSCS"></a>

**RSCS**\. Remote Spooling Communications Subsystem Networking\.

<a id="GLS RSU"></a>

**RSU**\. Recommended Service Upgrade

<a id="GLS"></a>

```
       ___
      | S |
      |___|
```

<a id="GLS saved segment"></a>

**saved segment**\. A segment of storage that has been saved and assigned a name\. The saved segments can be physical saved segments that CP recognizes or logical saved segments that CMS recognizes\. The segments can be loaded and shared among virtual machines, which helps use real storage more efficiently, or a private, nonshared copy can be loaded into a virtual machine\. See *logical saved segment* and *physical saved segment*\.

<a id="GLS saved system"></a>

**saved system**\. A special nonrelocatable copy of a virtual machine's virtual storage and associated registers kept on a CP\-owned disk and loaded by name instead of by I/O device address\. Loading a saved system by name substantially reduces the time it takes to IPL the system in a virtual machine\. Also, a saved system such as CMS can also share one or more 1MB segments of reenterable code in real storage between virtual machines\. This reduces the cumulative real main storage requirements and paging demands of such virtual machines\.

<a id="GLS screen"></a>

**screen**\. An illuminated display surface; for example, the display surface of a CRT\. Synonymous with *physical screen*\.

<a id="GLS SDO"></a>

**SDO**\. System delivery offering\.

<a id="GLS secondary user"></a>

**secondary user**\. When a user is disconnected \-\- that is, has no virtual console on line \-\- a secondary user can be designated to receive the disconnected user's console messages and to enter commands to the disconnected user's console\.

<a id="GLS segment"></a>

**segment**\. In System/370 architecture, 64KB of virtual storage\. In 370\-XA, ESA/370, ESA/390, and ESA/XC architecture, 1MB of virtual storage\.

<a id="GLS segment space"></a>

**segment space**\. A saved segment is composed of up to 64 member saved segments accessed by a single name\. A segment space occupies one or more architecturally\-defined segments\. It begins and ends on a 1MB boundary\. A user with access to a segment space has access to all of its members\. See *discontiguous saved segment*, *member saved segment*, *saved segment*, and *segment*\.

<a id="GLS select data file"></a>

**select data file**\. In VMSES/E, a file containing a list of the parts serviced by the VMFAPPLY EXEC\. The VMFAPPLY EXEC updates this file with a time stamp and a list of parts that were serviced\. The VMFBLD EXEC checks the select data file for build requirements and updates the objects that are affected by service to a status of 'SERVICED' in the service\-level build status table\. The select data file is named appid $SELECT, where appid is the apply ID\. See apply ID\.

<a id="GLS selective preventive service"></a>

**selective preventive service**\. The selective application of PTFs from a PUT or RSU\. Contrast with preventive service\.

<a id="GLS separator"></a>

**separator**\. Synonym for *delimiter*\.

<a id="GLS server"></a>

**server**\. The general name for a virtual machine that provides a service for a requesting virtual machine\.

<a id="GLS service"></a>

**service**\. Changing a product after installation\. See *corrective service, local service,* and *program update service*\.

<a id="GLS service level"></a>

**service level**\. The PTF and preventive service level that is associated with the testing level and support level of an orderable product function\.

<a id="GLS service level inventory"></a>

**service level inventory**\. see *service\-level Software Inventory*\.

<a id="GLS service - level Software Inventory"></a>

**service\-level Software Inventory**\. In VMSES/E, the level of the Software Inventories that contains: requisite relationships between PTFs, the status of PTFs installed, the service level of each part of the product and, the status of objects built for the product\.

<a id="GLS service machine"></a>

**service machine**\. A virtual machine running a program that provides system\-wide services\.

<a id="GLS service tape"></a>

**service tape**\. A tape containing service changes for one or more products\. See *corrective service tape* and *program update tape \(PUT\)*\.

<a id="GLS service virtual machine"></a>

**service virtual machine**\. A virtual machine that provides a system service such as accounting, error recording, monitoring, or that provided by a supported licensed program\.

<a id="GLS serviceable parts"></a>

**serviceable parts**\. The individual parts of a product that can be serviced separately\. A serviceable part has the file name of the source or replacement part and a file type in the form tttnnnnn, where ttt is a unique three\-character abbreviation for the part type and nnnnn is the PTF number\. Serviceable parts are maintained by both source updates and replacement service\.

<a id="GLS SERVICED"></a>

**SERVICED**\. This status, listed in the service\-level build status table, indicates that the object has been SERVICED but not built\.

<a id="GLS SFS"></a>

**SFS**\. Shared file system\.

<a id="GLS SFS directory"></a>

**SFS directory**\. A group of files\. SFS directories can be arranged to form a hierarchy in which one directory can contain one or more subdirectories as well as files\.

<a id="GLS Shared File System ( SFS )"></a>

**Shared File System \(SFS\)**\. A part of CMS that lets users organize their files into groups known as *directories* and selectively share those files and directories with other users\.

<a id="GLS shared segment"></a>

**shared segment**\. A feature of a saved system or physical saved segment that lets one or more segments of reentrant code or data in real storage be shared among many virtual machines\. For example, if a saved CMS system was generated, the CMS nucleus is shared in real storage among all CMS virtual machines loaded by name; that is, every CMS machine's segment of virtual storage maps to the same 1MB of real storage\. See *discontiguous saved segment* and *saved system*\.

<a id="GLS shared system"></a>

**shared system**\. See *saved system* and *shared read\-only system residence disk*\.

<a id="GLS simultaneous peripheral operations online ( SPOOL )"></a>

**simultaneous peripheral operations online \(SPOOL\)**\. \(1\) \(Noun\) An area of auxiliary storage defined to temporarily hold data during its transfer between peripheral equipment and the processor\. \(2\) \(Verb\) To use auxiliary storage as a buffer storage to reduce processing delays when transferring data between peripheral equipment and the processing storage of a computer\.

<a id="GLS single user group"></a>

**single user group**\. The concept in GCS of a virtual machine that runs applications that do not require group communications\. This allows an application to run without the overhead of group initialization and multiple virtual machines\. Multiple users can IPL the same saved system if it had been built for a single user environment\. See *virtual machine group*\.

<a id="GLS SNA"></a>

**SNA**\. Systems Network Architecture\.

<a id="GLS soft requisite"></a>

**soft requisite**\. The subset of a PTF's requisite that is not a hard requisite\. A PTF has a soft requisite for another PTF if it affects any of the same modules\. The relationship exists because the pre\-built replacement parts that are shipped with PTFs are built with all prior PTFs\.

<a id="GLS software inventory management"></a>

**software inventory management**\. Utilities provided by VMSES/E that provide a standard interface to the system level inventories, service level inventories, tool control statements \(TCS\), product parameter file \(PPF\), and file type abbreviation table\.

<a id="GLS software product"></a>

**software product**\. Any software supplied by IBM or an Original Equipment Manufacturer \(OEM\), or user written programs\. The term includes program offerings and program products \(PPs\)\.

<a id="GLS source code"></a>

**source code**\. The input to a compiler or assembler, written in a source language\. Contrast with *object code*\.

<a id="GLS source file"></a>

**source file**\. A file that contains source statements for such items as high\-level language programs and data description specifications\.

<a id="GLS source product parameter file"></a>

**source product parameter file**\. In VMSES/E, a file supplied with a product containing: recommended values for the options that control VMSES/E processing for the product, installation and service tape formats, and the list of build lists used to build the product\. The file name of the source product parameter file matches the prodid of the product and the file type is $PPF\. Source PPFs\.

<a id="GLS source update"></a>

**source update**\. A change to the original assembler code provided with a product\. VM source code is contained in files with a file type of ASSEMBLE\. To update an ASSEMBLE file, the user creates update files containing control statements that describe the changes to be made\.

<a id="GLS source update file"></a>

**source update file**\. A file containing a single change to a statement in a source file\. The file can also include requisite information for applying the change\. Synonymous with *update file*\.

<a id="GLS SPOOL"></a>

**SPOOL**\. Simultaneous peripheral operations online\.

<a id="GLS spool file"></a>

**spool file**\. A collection of data along with CCWs for processing on a unit record device\. Contrast with *system data file*\.

<a id="GLS spool ID"></a>

**spool ID**\. A spool file identification number automatically assigned by CP when the file is closed\. The spool ID number can be from 0001 to 9900; it is unique for each spool file\. To identify a given spool file, a user must specify the owner's user ID, the virtual device type, and the spool ID\.

<a id="GLS spooling"></a>

**spooling**\. The processing of files created by or intended for virtual readers, punches, and printers\. The spool files can be sent from one virtual device to another, from one virtual machine to another, and to real devices\. See *virtual console spooling*\.

<a id="GLS stand - alone dump"></a>

**stand\-alone dump**\. A dump acquired without regular system functions\. For example, to obtain a CP dump when the regular system is unable to dump the machine, the stand\-alone dump facility gets a CP stand\-alone dump\.

<a id="GLS string"></a>

**string**\. A group of minidisks defined for a specific function in the product parameter file, for example, the BASE2 string, which holds source code\.

<a id="GLS sub hard requisite"></a>

**sub hard requisite**\. In VMSES/E, a sub hard requisite is a hard requisite of an explicitly defined requisite\.

<a id="GLS sub if - requisite"></a>

**sub if\-requisite**\. In VMSES/E, a sub if\-requisite is an if\-requisite of an explicitly defined requisite\.

<a id="GLS subcommand"></a>

**subcommand**\. The commands of processors such as EDIT or XEDIT that run under CMS\.

<a id="GLS subdirectory"></a>

**subdirectory**\. Any SFS directory below a user's top directory\. The CREATE DIRECTORY command creates subdirectories\. There can be up to eight levels of subdirectories with no limit on the number of them at each level, other than overall DASD space limits\. Each level of a subdirectory is an additional identifier of up to 16 characters that is appended to next higher level subdirectory\.

<a id="GLS subrequisite"></a>

**subrequisite**\. A subrequisite is a prerequisite or corequisite or an explicitly defined requisite\. The requisite of requisites\.

<a id="GLS SUPED"></a>

**SUPED**\. This status, listed in the service\-level apply status table, indicates that the PTF has been superseded\.

<a id="GLS supersede"></a>

**supersede**\. When a PTF supersedes another PTF, it includes all of the APARs, parts and requisite relationships of the PTF it supersedes\.

<a id="GLS syntax"></a>

**syntax**\. The rules for the construction of a command or program\.

<a id="GLS system administrator"></a>

**system administrator**\. The person responsible for maintaining a computer system\.

<a id="GLS system DDR tape"></a>

**system DDR tape**\. A tape containing the image of a built system for each type of DASD\.

<a id="GLS system delivery offering ( SDO )"></a>

**system delivery offering \(SDO\)**\. A VM/ESA package that includes a subset of all VM products or components\. This package has a single point of order and delivery, is refreshed periodically, and is installed from one logical tape\. All products or components included with the package, and their requisite relationships, are tested to ensure the package functions as a system\.

<a id="GLS System disk"></a>

**System disk**\. In VMSES/E, a minidisk or SFS directory containing other products that are required during service\.

<a id="GLS system level inventory"></a>

**system level inventory**\. See *system\-level Software Inventory*\.

<a id="GLS system - level Software Inventory"></a>

**system\-level Software Inventory**\. Level of the Software Inventories that contains: requisite relationships between products or components, the status of the product or component on the system, mapping of product identifier to the name of the product parameter file used during installation, and mapping of PTF file type abbreviation to real CMS file type\.

<a id="GLS system offering"></a>

**system offering**\. A package containing VM/SP and associated products\.

<a id="GLS system profile"></a>

**system profile**\. An EXEC \(SYSPROF\) that resides in a saved system or on a system disk and called by CMS initialization\. It contains some initialization functions, and provides a means for installations to override the default CMS environment by tailoring the exec to suit the installation\.

<a id="GLS system restart"></a>

**system restart**\. The restart that allows reuse of previously initialized areas\. System restart usually requires less time than IPL\. See *warm start*\.

<a id="GLS Systems Network Architecture ( SNA )"></a>

**Systems Network Architecture \(SNA\)**\. The description of the logical structure, formats, protocols, and operational sequences for transmitting information units through and controlling the configuration and operation of networks\.

<a id="GLS System string"></a>

**System string**\. In VMSES/E, the set of System disks\.

<a id="GLS"></a>

```
       ___
      | T |
      |___|
```

<a id="GLS T - disk"></a>

**T\-disk**\. Synonym for *temporary disk*\.

<a id="GLS tailorable file"></a>

**tailorable file**\. any source level product file that requires user input in order for the product to work correctly\. \(An example is a PROFILE EXEC\.\)

<a id="GLS tailorings"></a>

**tailorings**\. Changes made to a source level product file to customize it for your own environment\.

<a id="GLS tape descriptor file"></a>

**tape descriptor file**\. A file containing a directory of the products on a service tape\.

<a id="GLS tape document"></a>

**tape document**\. A document describing the service procedure for a service tape\.

<a id="GLS target"></a>

**target**\. One of many ways to identify a line to be searched for by XEDIT\. A target can be specified as an absolute line number, a relative displacement from the current line, a line name, or a string expression\.

<a id="GLS Target disk"></a>

**Target disk**\. In VMESE/E, a minidisk of SFS directory to which tape files are received on which the objects are built\.

<a id="GLS Target string"></a>

**Target string**\. In VMSES/E, the set of Target disks\.

<a id="GLS task"></a>

**task**\. A basic unit of work used for the execution of a program or a system function\.

<a id="GLS temporary disk"></a>

**temporary disk**\. An area on a DASD available to the user for newly created or stored files until logoff, at which time the area is released\. Temporary disk space is allocated to the user during logon or when entering the CP DEFINE command\. Synonymous with *T\-disk*\.

<a id="GLS temporary product parameter file"></a>

**temporary product parameter file**\. In VMSES/E, the output of the VMFOVER EXEC\. The file name is either the file name of the last override product parameter file in the chain of overrides, or the file name of the source product parameter file\. The file type is $PPFTEMP\.

<a id="GLS terminal"></a>

**terminal**\. A device, usually equipped with a keyboard and a display, capable of sending and receiving information\.

<a id="GLS text deck"></a>

**text deck**\. An object\-code file that must be additionally processed to produce executable machine code\.

<a id="GLS text library"></a>

**text library**\. A CMS file that contains relocatable object modules and a directory that indicates the location of each of these modules within the library\.

<a id="GLS time stamp"></a>

**time stamp**\. A record containing the TOD clock value stored in its internal 32\-bit binary format\.

<a id="GLS time - of - day ( TOD ) clock"></a>

**time\-of\-day \(TOD\) clock**\. A hardware feature required by VM/ESA\. The TOD clock is incremented once every microsecond, and provides a consistent measure of elapsed time suitable for the indication of date and time; it runs regardless of the processor state \(running, wait, or stopped\)\.

<a id="GLS TOD clock"></a>

**TOD clock**\. Time\-of\-day clock\.

<a id="GLS token"></a>

**token**\. An eight\-character symbol created by the CMS EXEC processor when it scans an EXEC procedure or EDIT macro statements\. Symbols longer than eight characters are truncated to eight characters\.

<a id="GLS"></a>

```
       ___
      | U |
      |___|
```

<a id="GLS update file"></a>

**update file**\. Synonym for *source update file*\.

<a id="GLS update service"></a>

**update service**\. Servicing a part by applying a change to a source file statement, then assembling or compiling the source file to produce a new object file\.

<a id="GLS usable form"></a>

**usable form**\. In VMSES/E, a part of a product whose level cannot be identified from its file name or file type, the final objects which make up the product, a source file with the file type ASSEMBLE\. If the level can be identified from its file name the part is referred to as a serviceable part\.

<a id="GLS usable form product parameter file"></a>

**usable form product parameter file**\. Product parameter files used by the majority of VMSES/E execs\. The file name matches the file name of either the last override product parameter file in the chain of overrides, or the file name of the source product parameter file if there are no overrides\. The file type is PPF\.

<a id="GLS user"></a>

**user**\. Anyone who requests the services of a computing system\.

<a id="GLS user class"></a>

**user class**\. A privilege category assigned to a virtual machine user in the user's directory entry; each class specified allows access to a logical subset of all the CP commands\. See *privilege class*\.

<a id="GLS user exit"></a>

**user exit**\. An interface to VM/ESA that can be used by an application program\. Generally, a user exit affects only the particular application specifying the exit and is run as part of the application program\.

<a id="GLS user ID"></a>

**user ID**\. User identification\.

<a id="GLS user memo"></a>

**user memo**\. \(1\) At the system\-level, special instructions for installing a product, and \(2\) at the service\-level, special instructions for installing a PTF\.

<a id="GLS user modification"></a>

**user modification**\. Any change that a user originates for a product or component\.

<a id="GLS"></a>

```
       ___
      | V |
      |___|
```

<a id="GLS vaddr"></a>

**vaddr**\. Virtual address\.

<a id="GLS variable symbol"></a>

**variable symbol**\. In an EXEC procedure, a symbol beginning with an ampersand \(&amp;\) character, the value of which is assigned by the user, or sometimes by the VM/REXX interpreter, the EXEC 2 processor, or CMS EXEC processor\. The value of a variable symbol can be tested and changed using control statements\. See *special variable*\.

<a id="GLS version vector table"></a>

**version vector table**\. The Software Inventory table that identifies which PTFs have been applied to each part of the product and the current level of each part\. The file type of the service level inventory table is VVT*lvlid*\. The *lvlid* may be unique for each level of service the customer has installed for a product or component\. It corresponds directly to each AUX level in the control file\. The system level inventory does not contain this table\.

<a id="GLS virtual address"></a>

**virtual address**\. The address of a location in virtual storage\. A virtual address must be translated into a real address to process the data in processor storage\.

<a id="GLS virtual console"></a>

**virtual console**\. A console simulated by CP on a terminal such as a 3270\. The virtual device type and I/O address are defined in the VM/ESA directory entry for that virtual machine\.

<a id="GLS virtual console spooling"></a>

**virtual console spooling**\. The writing of console I/O on disk as a printer spool file instead of, or in addition to, having it typed or displayed at the virtual machine console\. The console data includes messages, responses, commands, and data from or to CP and the virtual machine operating system\. The user can invoke or terminate console spooling at any time\. When the console spool file is closed, it becomes a printer spool file\. Synonymous with *console spooling*\.

<a id="GLS virtual disk"></a>

**virtual disk**\. A logical subdivision \(or all\) of a physical disk storage device that has its own address, consecutive storage space for data, and an index or description of the stored data so that the data can be accessed\. A virtual disk is also called a minidisk\. See *disk*\.

<a id="GLS virtual machine ( VM )"></a>

**virtual machine \(VM\)**\. A functional equivalent of a computing system\. In VM/ESA, virtual machines can simulate the System/370, 370\-XA, ESA/370, and ESA/390 functions\. In addition, on ESA/390 systems, the XC virtual machine architecture is available\. Each virtual machine is controlled by an operating system\. VM controls the concurrent execution of several virtual machines on an actual processor complex\. See *370 virtual machine XA virtual machine*, *ESA virtual machine*, and *XC virtual machine*\.

<a id="GLS virtual machine group"></a>

**virtual machine group**\. The concept in GCS of two or more virtual machines associated with each other through the same named system \(for example, IPL GCS1\)\. Virtual machines in a group share common read/write storage and can communicate with one another through facilities provided by GCS\. Synonymous with *group*\. See *single user group*\.

<a id="GLS Virtual Machine / Enterprise Systems Architecture ( VM / ESA )"></a>

**Virtual Machine/Enterprise Systems Architecture \(VM/ESA\)**\. An IBM licensed program that manages the resources of a single computer so that multiple computing systems appear to exist\. Each virtual machine is the functional equivalent of a *real* machine\.

<a id="GLS virtual printer ( or punch )"></a>

**virtual printer \(or punch\)**\. A printer \(or card punch\) simulated on disk by CP for a virtual machine\. The virtual device type and I/O address are usually defined in the VM/ESA directory entry for that virtual machine\.

<a id="GLS virtual storage"></a>

**virtual storage**\. Storage space that can be regarded as addressable main storage by the user of a computer system in which virtual addresses are mapped into real addresses\. The size of virtual storage is limited by the addressing scheme of the computing system and by the amount of auxiliary storage available, not by the actual number of main storage locations\.

<a id="GLS virtual = real area ( V = R area )"></a>

**virtual=real area \(V=R area\)**\. The part of real storage, starting with real page 1, where a virtual=real machine can execute\. CP maintains control of real page zero; only page zero of the virtual=real machine is relocated\. Only one virtual machine at a time can occupy the virtual=real area\. The area must be defined during VM/ESA system generation to contain the largest virtual=real machine likely to run\. See *virtual=real option*\.

<a id="GLS VM"></a>

**VM**\. Virtual machine\.

<a id="GLS VM directory"></a>

**VM directory**\. A CP disk file that defines each virtual machine's typical configuration: the user ID, password, regular and maximum allowable virtual storage, CP command privilege class or classes allowed, dispatching priority, logical editing symbols to be used, account number, and CP options desired\. Synonymous with *CP directory*\.

<a id="GLS VM / ESA"></a>

**VM/ESA**\. Virtual Machine/Enterprise Systems Architecture\.

<a id="GLS VMLIB"></a>

**VMLIB**\. The name of the CSL supplied with VM/ESA and that contains routines to do various VM functions\.

<a id="GLS VMSES"></a>

**VMSES**\. A component of VM in VM/ESA Rel\. 1\.0 that provides the tools for installing and servicing the various components of the VM product\.

<a id="GLS VMSES / E"></a>

**VMSES/E**\. Virtual Machine Serviceability Enhancements Staged/Extended\.

<a id="GLS VMSES / E"></a>

**VMSES/E**\. A component of VM, first shipped in VM/ESA Rel\. 1\.1, that provides the tools for installing and servicing the various components of the VM product\. It is also the strategic installation and service tool for all of the other products that run on VM/ESA platforms\.

<a id="GLS VMSES / E installation / service tool"></a>

**VMSES/E installation/service tool**\. Consists of two VMSES/E user interfaces, VMFINS and VMFSIM, all of the VMSES/E commands, and the service\-level and system\-level Software Inventories\. Synonymous with *VMSES/E*\.

<a id="GLS volid"></a>

**volid**\. Volume identifier\.

<a id="GLS volume identifier ( volid )"></a>

**volume identifier \(volid\)**\. The volume identification label for a disk\.

<a id="GLS"></a>

```
       ___
      | W |
      |___|
```

<a id="GLS warm start"></a>

**warm start**\. \(1\) The result of an IPL that does not erase previous system data\. \(2\) The automatic reinitialization of the VM/ESA control program that occurs if the control program cannot continue processing\. Closed spool files and the VM/ESA accounting information are not lost\. Contrast with *checkpoint \(CKPT\) start, cold start*, and *force start*\.

<a id="GLS window"></a>

**window**\. An area on the physical screen where virtual screen data can be displayed\. Windowing lets the user do such functions as defining, positioning, and overlaying windows; scrolling backward and forward through data; and writing data into virtual screens\.

<a id="GLS write authority"></a>

**write authority**\. The authority to read or change the contents of a file or directory\. Write authority implies read authority\.

<a id="GLS"></a>

```
       ___
      | X |
      |___|
```

<a id="GLS XA mode"></a>

**XA mode**\. A GCS mode of operation on ESA that uses the full capabilities of the Extended Systems Architecture\.

<a id="GLS XEDIT"></a>

**XEDIT**\. The CMS facility, containing the XEDIT command and XEDIT subcommands and macros, that lets a user create, change, and manipulate CMS files\.

<a id="GLS XEDIT macro"></a>

**XEDIT macro**\. \(1\) A procedure defined by a frequently used command sequence to do a commonly required editing function\. A user creates the macro to save repetitious reentering of the sequence, and invokes the entire procedure by entering a command \(that is, the macro file's file name\)\. The procedure can consist of a long sequence of XEDIT commands and subcommands or both, and CMS and CP commands or both, along with REXX or EXEC 2 control statements to control processing within the procedure\. \(2\) A CMS file with a file type of *XEDIT*\.

<a id="GLS"></a>

```
       ___
      | Y |
      |___|
```

<a id="GLS Y - STAT"></a>

**Y\-STAT**\. A block of storage that contains the FSTs associated with file mode Y\. The FSTs are sorted so that a binary search can search for files\. The Y\-STAT usually resides in the CMS nucleus so it can be shared\. Only files with file mode of 2 will have their associated FSTs in the Y\-STAT\.

<a id="GLS"></a>

```
       ___
      | Z |
      |___|
```

<a id="GLS zap"></a>

**zap**\. To modify or dump an individual text file, using the ZAP command or the ZAPTEXT EXEC\.

<a id="GLS"></a>

```
       ___
      | 3 |
      |___|
```

<a id="GLS 3262"></a>

**3262**\. Refers to the IBM 3262 Printer, Models 1 and 11\.

<a id="GLS 3270"></a>

**3270**\. Refers to a series of IBM display devices, for example, the IBM 3275, 3276 Controller Display Station; 3277, 3278, and 3279 Display Stations; the 3290 Information Panel; and the 3287 and 3286 printers\. A specific device type is used only when a distinction is required between device types\. Information about display terminal usage also refers to the IBM 3138, 3148, and 3158 Display Consoles when used in display mode, unless otherwise noted\.

<a id="GLS 3284"></a>

**3284**\. Refers to the IBM 3284 Printer\. Information on the 3284 also pertains to the IBM 3286, 3287, 3288, and 3289 printers, unless otherwise noted\.

<a id="GLS 3380"></a>

**3380**\. Refers to the IBM 3380 Direct Access Storage Device\.

<a id="GLS 3390"></a>

**3390**\. Refers to the IBM 3390 Direct Access Storage Device\.

<a id="GLS 3422"></a>

**3422**\. Refers to the IBM 3422 Magnetic Tape Subsystem\.

<a id="GLS 3480"></a>

**3480**\. Refers to the IBM 3480 Magnetic Tape Subsystem\.

<a id="GLS 3490"></a>

**3490**\. Refers to the IBM 3490 Magnetic Tape Subsystem\.

<a id="GLS 370 mode"></a>

**370 mode**\. A GCS mode of operation on ESA that simulates 370 architecture\.

<a id="GLS 370 virtual machine"></a>

**370 virtual machine**\. A virtual machine that simulates System/370 functions\. Contrast with *XA virtual machine, ESA virtual machine,* and *XC virtual machine*\.

<a id="GLS 3800"></a>

**3800**\. Refers to the IBM 3800 Printing Subsystems\. A specific device type is used only when a distinction is required between device types\.

<a id="GLS"></a>

```
       ___
      | 4 |
      |___|
```

<a id="GLS 4245"></a>

**4245**\. Refers to the IBM 4245 Printer\.

<a id="GLS 4248"></a>

**4248**\. Refers to the IBM 4248 Printer\.

<a id="GLS 4250"></a>

**4250**\. Refers to the IBM 4250 Printer\.

<a id="GLS"></a>

```
       ___
      | 9 |
      |___|
```

<a id="GLS 9332"></a>

**9332**\. Refers to the IBM 9332 Direct Access Storage Device, Model 400\.

<a id="GLS 9335"></a>

**9335**\. Refers to the IBM 9335 Direct Access Storage Device, Models A01 and B01\.

<a id="GLS 9370"></a>

**9370**\. Refers to a series of processors, namely the IBM 9371 Models 10, 12, and 14, the IBM 9373 Model 20, the IBM 9375 Models 40 and 60, the IBM 9377 Model 90, and other models\.

<a id="GLS ."></a>

<a id="GLS ."></a>

<a id="GLS ."></a>

<a id="GLS ."></a>

<a id="GLS ."></a>

<a id="GLS ."></a>

<a id="GLS ."></a>

<a id="GLS ."></a>

<a id="GLS ."></a>

<a id="GLS ."></a>

<a id="GLS ."></a>

<a id="GLS"></a>

---

[Previous](g-5.md) | [Index](README.md) | [Next](bibliography.md)
