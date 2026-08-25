[Previous](11-1.md) | [Index](README.md) | [Next](abbreviations.md)

---

# GLOSSARY Glossary

Glossary

<a id="GLS"></a>

A

Advanced Program-to-Program Communication (APPC)

**. Adv**a**nced Program-to-Pr**o**gram Communic**a**tion (**APPC). An implementation of LU6.2 together with an application programming interface (API). Different platforms (MVS, RISC System/6000, OS/2) have different APPC implementations. A common, standard APPC API is defined, called the Common Programming Interface for Communications (CPI-C).

APPC/MVS

**APPC/MVS**. An MVS component that supplies APPC services to programs.

area

**area**. An area is a partition of a DEDB. It is a single data set, although there may be several identical copies of it (see multiple area data sets). An area contains all segment types.

<a id="GLS"></a>

B

base section

**base** **section**. A set of CIs in a DEDB unit of work. It is the place where IMS tries to store root segments and their direct dependents.

<a id="GLS"></a>

C

control interval (CI)

**control** **interval** **(CI)**. A logical block of data in a VSAM data set. VSAM read and write requests are issued against CIs.

control interval update sequence number (CUSN)

**control** **interval** **update** **sequence** **number** **(CUSN)**. A field that IMS maintains in every CI in the direct part of a DEDB. It is used to ensure data integrity after an IMS emergency restart in a block level data sharing environment.

conversation

**conversation**. In APPC, when two programs communicate with each other, they are said to be in "conversation." Every request to allocate a conversation specifies the APPC transaction program name.

commit (or sync) point

**commit** **(or** **sync)** **point**. The point in time when an application program completes the processing of a message or a batch unit of recovery. For a message-driven application, it is the time when the program asks for the . next message. For a batch program, it is the result of a CHKP or SYNC DL/1 call. Any changes the program has made to resources can now be made permanent. Any resources the program held can now be released.

Common Programming Interface for Communications (CPIC or CPI-C)

**Common** **Programming** **Interface** **for** **Communications** **(CPIC** **or** **CPI-C)**. An API for APPC that is standard across many environments.

Communications Manager/2 (CM/2)

**Communications** **Manager/2** **(CM/2)**. A product that supplies APPC and other communication services to OS/2 programs.

<a id="GLS"></a>

D

database control (DBCTL)

**database** **control** **(DBCTL)**. One of the execution modes of IMS. In this mode, IMS manages databases only. Terminal and transaction management is performed by an external subsystem such as CICS.

(DMAC)

**(DMAC)**. A DEDB control block that contains vital information about an area.

data entry database (DEDB)

**data** **entry** **database** **(DEDB)**. An IMS Fast Path database that provides high performance, high availability, and support for very large amounts of data. It also includes an extremely efficient data entry facility, after which it is named.

dependent overflow

**dependent** **overflow**. A set of CIs in a DEDB unit of work, providing first level overflow from the base section. It is the first place where IMS looks for space to insert a segment that will not fit in the most desirable CI in the base section. See also independent overflow.

direct dependent segment (DDEP)

**direct** **dependent** **segment** **(DDEP)**. An ordinary type of segment within a DEDB. It is a segment that is neither a root nor an SDEP.

<a id="GLS"></a>

E

Expedited Message Handling (EMH)

**Expedited** **Message** **Handling** **(EMH)**. The message processing component of IMS Fast Path.

Extended Recovery Facility (XRF)

**Extended** **Recovery** **Facility** **(XRF)**. An IMS facility for high availability using a hot-standby alternate system.

Extended Terminal Option (ETO)

**Extended** **Terminal** **Option** **(ETO)**. A function of IMS that allocates terminal and user control blocks dynamically at logon and sign-on times. The alternative is that static terminals are predefined in the IMS system definition.

<a id="GLS"></a>

F

Fast Path (FP)

**Fast** **Path** **(FP)**. A group of IMS functions that are simpler but faster than the rest of IMS's functions. Fast Path comprises DEDBs, MSDBs, and EMH.

field search argument (FSA)

**:H4** **field** **search a**r**gumen**t (FSA). A parameter you pass to a DL/1 Field (FLD) call. It specifies which actions IMS should perform on that field.

front-end switch (FES)

front-end switch (FES). A very efficient message routing facility in IMS that allows transactions for other systems (not necessarily IMS systems) to be forwarded to the appropriate target system. It includes facilities for keeping the input terminal in response mode, specifying a timeout period, and determining what to do in the event a reply comes back after timeout.

Full Function (FF)

**Full** **Function** **(FF)**. A collective term for IMS databases that are not part of Fast Path.

<a id="GLS"></a>

H

hierarchical direct access method (HDAM)

**hierarchical** **direct** **access** **method** **(HDAM)**. A type of IMS database where the hierarchical structure is maintained by pointers and a root segment's position is derived by randomizing the root key.

hierarchic indexed direct access method (HIDAM)

**hierarchic** **indexed** **direct** **access** **method** **(HIDAM)**. A type of IMS database that is similar to HDAM but uses an index to access root segments. HIDAM stores root segments in key sequence.

High Speed Sequential Processing (HSSP)

**High** **Speed** **Sequential** **Processing** **(HSSP)**. An especially fast process that you can invoke to process a DEDB sequentially in batch.

High Speed Sequential Retrieval (HSSR)

**High** **Speed** **Sequential** **Retrieval** **(HSSR)**. A separate program product (product number 5787-LAC) that provides faster sequential processing of IMS databases.

Highly Parallel Transaction System (HPTS)

**Highly** **Parallel** **Transaction** **System** **(HPTS)**. A new IBM architecture for processing transactions in a sysplex using a large number of loosely coupled computers. IMS 5.1 supports and utilizes the HPTS environment.

<a id="GLS"></a>

I

independent overflow (IOVF)

**. indepe**n**dent ove**r**flow (**IOVF). A part of a DEDB area. It is the place where IMS puts segments after it has filled the appropriate dependent overflow.

intersystem communication (ISC)

**intersystem** **communication** **(ISC)**. A method of communication between IMS systems, between IMS and CICS systems, or between CICS systems. IMS ISC uses the LU 6.1 communication protocol.

<a id="GLS"></a>

L

logical unit (LU)

**logical** **unit** **(LU)**. A node in the telecommunications network. A logical unit can be a printer, a terminal, a computer, or a program.

Logical Unit 6.1 (LU6.1)

**Logical** **Unit** **6.1** **(LU6.1)**. An obsolete protocol that programs use to communicate with one another. IMS and CICS use this protocol for ISC. The LU6.1 protocol has been replaced by LU6.2.

Logical Unit 6.2 (LU6.2)

**Logical** **Unit** **6.2** **(LU6.2)**. An advanced protocol that programs use to communicate with one another (synonymous with APPC). LU6.2 is the preferred protocol for most advanced applications.

<a id="GLS"></a>

M

main storage database (MSDB)

**main** **storage** **database** **(MSDB)**. A type of IMS database held in main storage. The DEDB VSO option provides a preferred method of holding data in memory.

mode

**mode**. A VTAM control block describing the attributes of a network connection.

multiple area data sets (MADS)

**multiple** **area** **data** **sets** **(MADS)**. An option for a DEDB area to be replicated with up to seven copies, each of which is an area data set (ADS). This provides very high reliability and availability for the database data content.

Multiple Systems Coupling (MSC)

**Multiple** **Systems** **Coupling** **(MSC)**. A method of communicating between IMS systems. It is independent of the actual connection mechanism.

<a id="GLS"></a>

N

normal buffer allocation (NBA)

**normal** **buffer** **allocation** **(NBA)**. For a dependent region, the number of buffers in the Fast Path common pool that programs should normally find sufficient. In exceptional cases, more buffers can be allocated up to an additional maximum called the overflow buffer allocation (OBA). Only one program at a time, in the whole IMS system, can be using its OBA.

<a id="GLS"></a>

O

output thread

**output** **thread**. An output thread is a separate task that IMS uses to write updated DEDB CIs asynchronously to DASD.

overflow buffer allocation (OBA)

**overflow** **buffer** **allocation** **(OBA)**. The number of extra Fast Path database buffers a region can use above its NBA. IMS does not permit a program to exceed this limit.

<a id="GLS"></a>

P

partner

**partner**. In APPC one of the two parties in a conversation.

Parallel Sysplex (PS)

**Parallel** **Sysplex** **(PS)**. A new IBM architecture for running a sysplex of loosely coupled computers. Supported by IMS 5.1.

Parallel Transaction Server (PTS)

**Parallel** **Transaction** **Server** **(PTS)**. A new IBM computer architecture using CMOS technology which implements a parallel sysplex. Supported by IMS 5.1.

<a id="GLS"></a>

R

randomizer

**randomizer**. A program that uses the key of a segment to decide its position in the database. It is much faster than searching via an index. DEDBs and HDAM databases use randomizers. The randomizer does not select an arbitrary position for the segment; it chooses one of the predefined root anchor points (RAPs).

relative byte address (RBA)

**relative** **byte** **address** **(RBA)**. A position within a data set, measured in . bytes from the start. The first byte has an RBA of zero, the second byte, an RBA of 1 (because it is 1 byte from the start), and so on.

root addressable part

**root** **addressable** **part**. A part of a DEDB area. It is the collective term for all base and dependent overflow CIs.

root anchor point (RAP)

root anchor point (RAP). A special kind of pointer in a DEDB or HDAM database. A RAP points to a root segment. The randomizer chooses which RAP is assigned to each root.

<a id="GLS"></a>

S

side information

**side** **information**. (1) A name (for example, TP name, LU name) in a table of a set of APPC information that identifies an APPC application program. Another application can use this single name to represent the full APPC address.

sequential dependent part

**sequential** **dependent** **part**. A part of a DEDB area. It is the place where IMS stores SDEP segments.

sequential dependent (SDEP) segment

**sequential** **dependent** **(SDEP)** **segment**. A special type of segment within a DEDB, stored in the sequential dependent part, that is inserted very efficiently using a standard ISRT call. It is designed to be retrieved in bulk with the SDEP Scan utility, and deleted, to make space for reuse, by the SDEP Delete utility.

subset pointer (SSP)

**subset** **pointer** **(SSP)**. A special kind of database pointer that an application program may set and use. Only DEDBs support subset pointers.

sync point

**sync** **point**. Another name for commit point.

synonym chaining

**synonym** **chaining**. A phenomenon of DEDBs and HDAM databases. These databases use a randomizer to select a root anchor point off which to chain the roots. Sometimes the randomizer places more than one root at the same RAP. These two roots are then said to be synonyms of each other. IMS maintains a chain of pointers from one synonym to the next. This is called a "synonym chain." IMS maintains the chain in key sequence. The RAP points to the first root in the chain. See randomizer and root anchor point.

<a id="GLS"></a>

T

transaction program (TP)

**transaction** **program** **(TP)**. Another name for an APPC application program.

transaction program instance (TPI)

**transaction** **program** **instance** **(TPI)**. An executable instance of an application program. Each APPC conversation creates a separate TPI.

transcation program name (TPN)

**transcation** **program** **name** **(TPN)**. The name of the application program to be executed by an APPC allocate request. Often called a TP.

<a id="GLS"></a>

U

unit of work

**unit** **of** **work**. (1) The smallest amount of application program processing that is internally consistent. All elements of a unit of work are committed or backed out together. (2) In a DEDB, a unit of work is a predetermined number of contiguous CIs in the root-addressable part of the database.

<a id="GLS"></a>

V

virtual storage constraint relief (VSCR)

**virtual** **storage** **constraint** **relief** **(VSCR)**. Certain areas of virtual storage are in short supply. Successive releases of IMS and other products have sought to reduce their use of these areas of virtual storage. This is known as virtual storage constraint relief.

Virtual Storage Option (VSO)

**Virtual** **Storage** **Option** **(VSO)**. An option for DEDBs that allows the data to be kept in memory in an MVS data space. Periodically, IMS asynchronously writes any updated CIs back to the area data sets.

<a id="GLS"></a>

W

Wait for Input (WFI)

**Wait** **for** **Input** **(WFI)**. A type of IMS application program that remains in the dependent region even when there is no immediate message to process.

<a id="GLS"></a>

---

[Previous](11-1.md) | [Index](README.md) | [Next](abbreviations.md)
