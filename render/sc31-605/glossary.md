[Previous](3-30.md) | [Index](README.md) | [Next](bibliography.md)

---

# GLOSSARY Glossary

<a id="HDRGLOSSRY"></a>

Glossary This glossary defines important NCP, NetView, NetView/PC, SSP, and VTAM

This glossary defines important NCP, NetView, NetView/PC, SSP, and VTAM

abbreviations and terms. It includes information from the IBM Dictionary of Computing, SC20-1699. Definitions from the American National Dictionary for Information Processing are identified by an asterisk (*). Definitions from draft proposals and working papers under development by the International Standards Organization, Technical Committee 97, Subcommittee 1 are identified by the symbol **(TC97)** Definitions from the CCITT Sixth Plenary Assembly Orange Book, Terms and Definitions and working documents published by the Consultative Committee on International Telegraph and Telephone of the International Telecommunication Union, Geneva, 1980 are preceded by the symbol **(CCITT/ITU)**. Definitions from published sections of the ISO Vocabulary of Data Processing, developed by the International Standards Organization, Technical Committee 97, Subcommittee 1 and from published sections of the ISO Vocabulary of Office Machines developed by subcommittees of ISO Technical Committee 95, are preceded by the symbol **(ISO)** For abbreviations, the definition usually consists only of the words represented by the letters; for complete definitions, see the entries for the words. **Reference** **Words** **Used** **in** **the** **Entries** The following reference words are used in this glossary: *Deprecated* *term* *for*. Indicates that the term should not be used. It refers to a preferred term, which is defined. Syno*nymous wit*h*. A*ppears in the commentary of a preferred term and identifies less desirable or less specific terms that have the same meaning. Syno*nym for*. *A*ppears in the commentary of a less desirable or less specific term and identifies the preferred term that has the same meaning. *Contrast* *with*. Refers to a term that has an opposed or substantively different meaning. See. *R*efers to multiple-word terms that have the same last word. *See* *also*. Refers to related terms that have similar (but not synonymous) meanings..

<a id="GLS"></a>

A

abend

**abend**. Abnormal end of task.

abnormal end of task (abend)

abnormal end of task (abend). Termination of a task before its completion

because? of? an error condition that cannot be resolved by recovery facilities while the task is executing.

ACB name

**ACB** **name**. (1) The name of an ACB macroinstruction. (2) A name specified in the ACBNAME parameter of a VTAM APPL statement. Contrast with *network* *name*.

accept

**accept**. (1) For a VTAM application program, to establish a session with a logical unit (LU) in response to a CINIT request from a system services control point (SSCP). The session-initiation request may begin when a terminal user logs on, a VTAM application program issues a macroinstruction, or a VTAM operator issues a command. See also *acquire* *(1)*. (2) An SMP process that moves distributed code and MVS-type programs to the distribution libraries.

access barred

**access** **barred**. The state in which the calling data terminal equipment (DTE) is not permitted to make a call to the DTE identified by the selection signals.

access method

**access** **method**. A technique for moving data between main storage and input/output devices.

ACF

**ACF**. Advanced Communications Function.

ACF/NCP

**ACF/NCP**. Advanced Communications Function for the Network Control Program. Synonym for *NCP*.

ACF/SSP

**ACF/SSP**. Advanced Communications Function for the System Support Programs. Synonym for *SSP*.

ACF/VTAM

**ACF/VTAM**. Advanced Communications Function for the Virtual Telecommunications Access Method. Synonym for *VTAM*.

acquire

**acquire**. (1) For a VTAM application program, to initiate and establish a session with another logical unit (LU). The acquire process begins when the application program issues a macroinstruction. See also *accept*. (2) To take over resources that were formerly controlled by an access method in another domain, or to resume control of resources that were controlled by this domain but released. Contrast with *release*. See also *resource* *takeover*.

active

**active**. (1) The state a resource is in when it has been activated and is operational. Contrast with *inactive,* *pending,* and *inoperative*. (2) Pertaining to a major or minor node that has been activated by VTAM. Most resources are activated as part of VTAM start processing or as the result of a VARY ACT command.

adapter

**adapter**. Hardware card that allows a device, such as a PC, to communicate with another device, such as a monitor, a printer, or other I/O device.

Advanced Communications Function (ACF)

**Advanced** **Communications** **Function** **(ACF)**. A group of IBM licensed programs (principally VTAM, TCAM, NCP, and SSP) that use the concepts of Systems Network Architecture (SNA), including distribution of function and resource sharing.

aggregate link

**aggregate** **link**. In the NetView Graphic Monitor Facility, a link that represents a collection of real links or lower-level aggregates. Examples are the transmission group that represents a collection of SDLC links, and the cluster link that represents a collection of transmission groups. An SNA transmission group is an aggregate link composed of SDLC links or channels; an SDLC link is an aggregate link composed of link stations and *link* connectors. See *also* link and real *link.*

aggregate node

**aggregate** **node**. In the NetView Graphic Monitor Facility, a node that represents either a collection of real resources or a collection of lower-level aggregate nodes. An example is a backbone node that represents a communication controller and associated peripheral resources. See *also* *node* and real *node.*

alert

**alert**. (1) In SNA, a record sent to a system problem management focal point to communicate the existence of an alert condition. (2) In the NetView program, a high priority event that warrants immediate attention. This data base record is generated for certain event types that are defined by user-constructed filters.

application program

**application** **program**. (1) A program written for or by a user that applies to the user's work. (2) A program used to connect and communicate with stations in a network, enabling users to perform application-oriented activities.

attaching device

**attaching** **device**. Any device that is physically connected to a network and can communicate over the network.

auto removal

**auto** **removal**. Removing a device from the data passing activity without human intervention. This action is accomplished by the adapter.

auto-baud

**auto-baud**. In CCP, a line speed designation by which the IBM 3710 Network Controller determines the line speed.

automation table

**automation** **table**. A NetView member or file that is used to define what messages and MSUs are to be automated and what automated actions should take place. See *automation* *table* *statement,* *message,* *MSU.*

automation table statement

**automation** **table** **statement**. A statement used in the definition of a NetView automation table. Possible automation table statements are: IF, ALWAYS, END and SYN.

autotask

**autotask**. An unattended NetView operator station task that does not require a terminal or a logged-on user. Autotasks can run independent of VTAM and are typically used for automated console operations. Contrast with *logged-on* *operator*.

available

**available**. In VTAM, pertaining to a logical unit that is active, connected, enabled, and not at its session limit..

<a id="GLS"></a>

B

beacon

**beacon**. A frame sent by an adapter indicating a serious ring problem, such as a broken cable. An adapter is said to be *beaconing* if it is sending such a frame.

bidder

**bidder**. In SNA, the LU-LU half-session defined at session activation as having to request and receive permission from the other LU-LU half-session to begin a bracket. Contrast with *first* *speaker*. See also *bracket* *protocol* and *contention*.

binary synchronous communication (BSC)

**binary** **synchronous** **communication** **(BSC)**. (1) Communication using binary synchronous line discipline. (2) A uniform procedure, using a standardized set of control characters and control character sequences, for synchronous transmission of binary-coded data between stations.

BIU segment

**BIU** **segment**. In SNA, the portion of a basic information unit (BIU) that is contained within a path information unit (PIU). It consists of either a request/response header (RH) followed by all or a portion of a request/response unit (RU), or only a portion of an RU.

blocking of PIUs

**blocking** **of** **PIUs**. In SNA, an optional function of path control that combines multiple path information units (PIUs) into a single basic transmission unit (BTU).

boundary function

**boundary** **function**. (1) A capability of a subarea node to provide protocol support for attached peripheral nodes, such as: (a) interconnecting subarea path control and peripheral path control elements, (b) performing session sequence numbering for low-function peripheral nodes, and (c) providing session-level pacing support. (2) The component that provides these capabilities. See also *boundary* *node*, *network* *addressable* *unit* *(NAU)*, *peripheral* *path* *control*, *subarea* *node*, and *subarea* *path* *control*.

boundary node

**boundary** **node**. (1) A *subarea* node with boundary function. See subarea *node*. See also *boundary* *function*. (2) The programming component that performs FID2 (format identification type 2) conversion, channel data link control, pacing, and channel or device error recovery procedures for a locally attached station. These functions are similar to those performed by a network control program for an NCP-attached station.

bracket protocol

**bracket** **protocol**. In SNA, a data flow control protocol in which exchanges between the two LU-LU half-sessions are achieved through the use of brackets, with one LU designated at session activation as the first speaker and the other as the bidder. The bracket protocol involves bracket initiation and termination rules. See also *bidder* and *first* *speaker*.

browse

**browse**. (1) To look at records in a file. (2) In the NetView Graphic Monitor Facility, to look at a view that does not change when status changes. Contrast with *monitor*.

BSC

**BSC**. Binary synchronous communication.

buffer

**buffer**. A portion of storage for temporarily holding input or output data..

<a id="GLS"></a>

C

call

**call**. (1) * (ISO) The action of bringing a computer program, a routine, or a subroutine into effect, usually by specifying the entry conditions and jumping to an entry point. (2) To transfer control to a procedure, program, routine, or subroutine. (3) The actions necessary to make a connection between two stations. (4) To attempt to contact a user, regardless of whether the attempt is successful.

call connected packet

**call** **connected** **packet**. A call supervision packet that a data circuit-terminating equipment (DCE) transmits to indicate to a calling data terminal equipment (DTE) that the connection for the call has been completely established.

call request packet

**call** **request** **packet**. A call supervision packet that a data terminal equipment (DTE) transmits to ask that a connection for a call be established throughout the network.

call-accepted packet

**call-accepted** **packet**. * (ISO) A call supervision packet that a called data terminal equipment (DTE) transmits to indicate to the data circuit-terminating equipment (DCE) that it accepts the incoming call.

CALLOUT

**CALLOUT**. The logical channel type on which the data terminal equipment (DTE) can send a call, but cannot receive one.

carrier

**carrier**. On broadband networks, a continuous frequency signal that can be modulated with an information-carrying signal.

CBX

**CBX**. Computerized branch exchange.

CCU

**CCU**. Central control unit.

chain

**chain**. *(1*) A *group* of logically linked records. (2) See RU chain.

channel

**channel**. A path along which signals can be sent, for example, data channel, output channel. See *data* *channel* and *input/output* *channel*. See also *link*.

channel adapter

**channel** **adapter**. A communication controller hardware unit used to attach the controller to a System/360 or a System/370 channel.

channel-attached

**channel-attached**. (1) Pertaining to the attachment of devices directly by input/output channels to a host processor. (2) Pertaining to devices attached to a controlling unit by cables, rather than by telecommunication lines. Contrast with *link-attached*. Synonymous with *local*.

circuit

**circuit**. See *data* *circuit*.

circuit switching

**circuit** **switching**. (1) * (ISO) A process that, on demand, connects two or more data terminal equipments (DTEs) and permits the exclusive use of a data circuit between them until the connection is released. (2) Synonymous with *line* *switching*. (3) See also *message* *switching* and *packet* *switching*.

clear indication packet

**clear** **indication** **packet**. A call supervision packet that a data circuit-terminating equipment (DCE) transmits to inform a data terminal equipment (DTE) that a call has been cleared.

clear request packet

**clear** **request** **packet**. A call supervision packet transmitted by a data terminal equipment (DTE) to ask that a call be cleared.

cluster

**cluster**. Synonymous with *cluster* *node*.

cluster node

**cluster** **node**. In the NetView Graphic Monitor Facility, an aggregate node in a cluster view. It represents one or more backbone nodes and one or more backbone links.

code point

**code** **point**. In the NetView/PC program and in the NetView program, a 1- or 2-byte hexadecimal value that indexes a text string stored at an alert receiver and is used by the alert receiver to create displays of alert information.

command

**command**. (1) A request from a terminal for the performance of an operation or the execution of a particular program. (2) In SNA, any field set in the transmission header (TH), request header (RH), and sometimes portions of a request unit (RU), that initiates an action or that begins a protocol; for example: (a) Bind Session (session-control request unit), a command that activates an LU-LU session, (b) the change-direction indicator in the RH of the last RU of a chain, (c) the virtual route reset window indicator in a FID4 transmission header. See also *VTAM* *operator* *command*.

communication adapter

**communication** **adapter**. An optional hardware feature, available on certain processors, that permits communication lines to be attached to the processors.

Communication Control Program (CCP)

**Communication** **Control** **Program** **(CCP)**. A portion of the network control program communication interrupt control program (CICP) that initiates and ends I/O line operations, handles first-level line error recovery and recording, and administers commands issued by background programs.

communication controller

**communication** **controller**. A type of communication control unit whose operations are controlled by one or more programs stored and executed in the unit; for example, the IBM 3725 Communication Controller. It manages the details of line control and the routing of data through a network.

communication line

**communication** **line**. Deprecated term for *telecommunication* *line* and *transmission* *line*.

communication management configuration host node

**communication** **management** **configuration** **host** **node**. The type 5 host processor in a communication management configuration that does all network-control functions in the network except for the control of devices channel-attached to data hosts. Synonymous with *communication* *management* *host*. Contrast *with* *data* *host* node.

communication management host

**communication** **management** **host**. Synonym for *communication* *management* *configuration* *host* *node*. Contrast *with* *data* host.

component

**component**. Any part of a network other than an attaching device, such as an access unit.

composite end node (CEN)

**composite** **end** **node** **(CEN)**. A group of nodes made up of a single type 5 node and its subordinate type 4 nodes that together support type 2.1 protocols. To a type 2.1 node, a CEN appears as one end node. For example, NCP and VTAM act as a composite end node.

computerized branch exchange (CBX)

**computerized** **branch** **exchange** **(CBX)**. An exchange in which a central node acts as a high-speed switch to establish direct connections between pairs of attached nodes.

configuration

**configuration**. (1) (TC97) The arrangement of a computer system or network as defined by the nature, number, and the chief characteristics of its functional units. The term may refer to a hardware or a software configuration. (2) The devices and programs that make up a system, subsystem, or network. (3) In CCP, the arrangement of controllers, lines, and terminals attached to an IBM 3710 Network Controller. Also, the collective set of item definitions that describe such a configuration.

connection

**connection**. Synonym for *physical* *connection*.

contention

**contention**. A situation in which two logical units (LUs) that are connected by an LU 6.2 session both attempt to allocate the session for a conversation at the same time. The control operator assigns "winner" and "loser" status to the LUs so that processing may continue on an orderly basis. The contention loser requests permission from the contention winner to allocate a conversation on the session, and the contention winner either grants or rejects the request. See also *bidder*.

control block

**control** **block**. (1) (ISO) A storage area used by a computer program to hold control information. (2) In the IBM Token-Ring Network, a specifically formatted block of information provided from the application program to the Adapter Support Interface to request an operation.

control program (CP)

**control** **program** **(CP)**. The VM operating system that manages the real processor's resources and is responsible for simulating System/370s for individual users.

controller

**controller**. A unit that controls input/output operations for one or more devices.

coupler

**coupler**. A hardware device that connects a modem to a public phone system in much the same way that a telephone does..

<a id="GLS"></a>

D

DASD

**DASD**. Direct access storage device.

data channel

**data** **channel**. Synonym for *input/output* *channel*. See *channel*.

data check

**data** **check**. An indication that a transmission is faulty. For example, in SDLC a frame check sequence (FCS) error.

data circuit

**data** **circuit**. (1) (ISO) A pair of associated transmit and receive channels that provide a means of two-way data communication. (2) See also

physical circuit and virtual circuit .

Between? data switching exchanges, the data circuit may include data circuit-terminating equipment (DCE), depending on the type of interface used at the data switching exchange. Between a data station and a data switching exchange or data concentrator, the data circuit includes the data circuit-terminating equipment at the data station end, and may include equipment similar to a DCE at the data switching exchange or data concentrator location.

data circuit-terminating equipment (DCE)

**data** **circuit-terminating** **equipment** **(DCE)**. (TC97) The equipment installed at the user's premises that provides all functions required to establish, maintain, and terminate a connection, and the signal conversion and coding between the data terminal equipment (DTE) and the line. The DCE may be separate equipment or an integral part of other equipment.

data host

**data** **host**. Synonym for *data* *host* *node*. Contrast with *communication* *management* *configuration* *host*.

data host node

**data** **host** **node**. In a communication management configuration, a type 5 host node that is dedicated to processing applications and does not control network resources, except for its channel-attached or communication adapter-attached devices. Synonymous with *data* *host*. Contrast with *communication* *management* *configuration* *host* *node*.

data link

**data** **link**. In *SNA,* synonym for link.

data link control (DLC) layer

**data** **link** **control** **(DLC)** **layer**. In SNA, the layer that consists of the link stations that schedule data transfer over a transmission medium connecting two nodes and perform error control for the link connection. Examples of data link control are SDLC for serial-by-bit link connection and data link control for the System/370 channel.

data link control protocol

**data** **link** **control** **protocol**. In SNA, a set of rules used by two nodes on a data link to accomplish an orderly exchange of information. Synonymous with *line* *control*.

data link level

**data** **link** **level**. In the hierarchical structure of a data station, the conceptual level of control or processing logic between high level logic and the data link that maintains control of the data link. The data link level performs such functions as inserting transmit bits and deleting receive bits; interpreting address and control fields; generating, transmitting, and interpreting commands and responses; and computing and interpreting frame check sequences. See also *packet* *level* and *physical* *level*.

data packet

**data** **packet**. At the interface between a data terminal equipment (DTE) and a data circuit-terminating equipment (DCE), a packet used to transmit user data over a virtual circuit.

data terminal equipment (DTE)

**data** **terminal** **equipment** **(DTE)**. (TC97) That part of a data station that serves as a data source, data link, or both, and provides for the data communication control function according to protocols.

DCE

**DCE**. Data circuit-terminating equipment.

DCE clear confirmation packet

**DCE** **clear** **confirmation** **packet**. A call supervision packet that a data circuit-terminating equipment (DCE) transmits to confirm that a call has been cleared.

decryption

**decryption**. The unscrambling of data using an algorithm which works under the control of a key. The key allows data to be protected even when the algorithm is unknown. Data is unscrambled after transmission. Contrast with *encryption*.

definite response (DR)

**definite** **response** **(DR)**. In SNA, a value in the form-of-response-requested field of the request header. The value directs the receiver of the request to return a response unconditionally, whether positive or negative, to that request. Contrast with *exception* *response* and *no* *response*.

definition statement

**definition** **statement**. (1) In VTAM, the statement that describes an element of the network. (2) In NCP, a type of instruction that defines a resource to the NCP. See also *macroinstruction*.

delayed alert

**delayed** **alert**. An alert reporting a condition that prevented the sender from sending the alert to a control point. The fact that a delayed alert is sent implies that the condition it reports is no longer impacting availability. See *held* *alert*.

device

**device**. An input/output unit such as a terminal, display, or printer. See *attaching* *device*.

direct access storage device (DASD)

**direct** **access** **storage** **device** **(DASD)**. A device in which the access time is effectively independent of the location of the data. For example, a disk.

directory

**directory**. In VM, a control program (CP) file that defines each virtual machine's normal configuration.

disabled

**disabled**. In VTAM, pertaining to a logical unit (LU) that has indicated to its system services control point (SSCP) that it is temporarily not ready to establish LU-LU sessions. An initiate request for a session with a disabled logical unit (LU) can specify that the session be queued by the SSCP until the LU becomes enabled. The LU can separately indicate whether this applies to its ability to act as a primary logical unit (PLU) or a secondary logical unit (SLU). See also *enabled* and *inhibited*.

discarded packet

**discarded** **packet**. A packet that is intentionally destroyed.

Disk Operating System (DOS)

**Disk** **Operating** **System** **(DOS)**. Software for the PC that controls the execution of programs. Its full name is the IBM Personal Computer Disk Operating System.

display

**display**. (1) To present information for viewing, usually on a terminal screen or a hard-copy device. (2) A device or medium on which information is presented, such as a terminal screen. (3) Deprecated term for *panel*.

domain

**domain**. (1) An access method, its application programs, communication controllers, connecting lines, modems, and attached terminals. (2) In SNA, a system services control point (SSCP) and the physical units (PUs), logical units (LUs), links, link stations, and all the associated resources that the SSCP has the ability to control by means of activation requests and deactivation requests. See *system* *services* *control* *point* *domain* and *type* *2.1* *node* *control* *point* *domain*. See also *single-domain* *network* and *multiple-domain* *network*.

domain operator

**domain** **operator**. In a multiple-domain network, the person or program that controls the operation of the resources controlled by one system services control point. Contrast with *network* *operator* (2).

DOS

**DOS**. Disk Operating System.

double-byte character set (DBCS)

**double-byte** **character** **set** **(DBCS)**. A character set, such as kanji, in which each character is represented by a two-byte code.

drop

**drop**. In the IBM Token-Ring Network, a cable that leads from a faceplate to the distribution panel in a wiring closet. When the IBM Cabling System is used with the IBM Token-Ring Network, a drop may form part of a lobe.

DTE

**DTE**. Data terminal equipment.

dump

**dump**. (1) Computer printout of storage. (2) To write the contents of all or part of storage to an external medium as a safeguard against errors or in connection with debugging. (3) (ISO) Data that have been dumped..

<a id="GLS"></a>

E

ECB

**ECB**. Event control block.

echo

**echo**. The return of characters to the originating SS device to verify that a message was sent correctly.

ED

**ED**. Enciphered data.

Emulation Program (EP)

**Emulation** **Program** **(EP)**. An IBM control program that allows a channel-attached 3705 or 3725 communication controller to emulate the functions of an IBM 2701 Data Adapter Unit, an IBM 2702 Transmission Control, or an IBM 2703 Transmission Control. See also *network* *control* *program*.

enabled

**enabled**. In VTAM, pertaining to a logical unit (LU) that has indicated to its system services control point (SSCP) that it is now ready to establish LU-LU sessions. The LU can separately indicate whether this prevents it from acting as a primary logical unit (PLU) or as a secondary logical unit (SLU). See also *disabled* and *inhibited*.

enciphered data (ED)

**enciphered** **data** **(ED)**. Data whose meaning is concealed from unauthorized users.

encryption

**encryption**. The scrambling or encoding of data using an algorithm which works under the control of a key. The key allows data to be protected even when the algorithm is unknown. Data is scrambled prior to transmission. Contrast with *decryption*.

end node

**end** **node**. A type 2.1 node that does not provide any intermediate routing or session services to any other node. For example, APPC/PC is an end node. See *composite* *end* *node*, *node*, and *type* *2.1* *node*.

end user

**end** **user**. In SNA, the ultimate source or destination of application data flowing through an SNA network. An end user may be an application program or a terminal operator.

end-of-transmission (EOT)

**end-of-transmission** **(EOT)**. The specific character, or sequence of characters, that indicates no more data.

end-of-transmission (EOT) handshaking

**end-of-transmission** **(EOT)** **handshaking**. When a 3710 sends EOT characters over an idle line and waits for return characters. If no EOT response is returned, the 3710 breaks the session.

EOT

**EOT**. End-of-transmission.

EP

**EP**. Emulation Program.

error-to-traffic (E/T)

**error-to-traffic** **(E/T)**. The number of temporary errors compared to the traffic associated with a resource.

event

**event**. (1) In the NetView program, a record indicating irregularities of operation in physical elements of a network. (2) An occurrence of significance to a task; typically, the completion of an asynchronous operation, such as an input/output operation.

event control block (ECB)

**event** **control** **block** **(ECB)**. A control block used to represent the status of an event.

exception response (ER)

**exception** **response** **(ER)**. In SNA, a value in the form-of-response-requested field of a request header (RH). An exception response is sent only if a request is unacceptable as received or cannot be processed. Contrast with *definite* *response* and *no* *response*. See also *negative* *response*.

exchange identification (XID)

**exchange** **identification** **(XID)**. A data link control command and response passed between adjacent nodes that allows the two nodes to exchange identification and other information necessary for operation over the data link.

EXEC

**EXEC**. In a VM operating system, a user-written command file that contains CMS commands, other user-written commands, and execution control statements, such as branches.

explicit focal point (EP)

**explicit** **focal** **point** **(EP)**. An assigned focal point for which the set of nodes to be included in its sphere of control is defined locally. An explicit focal point initiates the exchange that brings a node into its sphere of control. Contrast with *implicit* *focal* *point.*

extended architecture (XA)

**extended** **architecture** **(XA)**. An extension to System/370 architecture that takes advantage of continuing high performance enhancements to computer system hardware..

<a id="GLS"></a>

F

fault domain

**fault** **domain**. In IBM Token-Ring Network problem determination, the portion of a ring that is involved with an indicated error.

FCS

**FCS**. Frame check sequence.

feature

**feature**. A particular part of an IBM product that a customer can order separately.

first speaker

**first** **speaker**. In SNA, the LU-LU half-session defined at session activation as: (1) able to begin a bracket without requesting permission from the other LU-LU half-session to do so, and (2) winning contention if both half-sessions attempt to begin a bracket simultaneously. Contrast with *bidder*. See also *bracket* *protocol*.

frame

**frame**. (1) The unit of transmission in some local area networks, including the IBM Token-Ring Network. It includes delimiters, control characters, information, and checking characters. (2) In SDLC, the vehicle for every command, every response, and all information that is transmitted using SDLC procedures.

frame check sequence (FCS)

**frame** **check** **sequence** **(FCS)**. A field immediately preceding the closing flag sequence of a frame that contains a bit sequence checked by the receiver to detect transmission errors..

<a id="GLS"></a>

G

generation

**generation**. The process of assembling and link editing definition statements so that resources can be identified to all the necessary programs in a network.

graphic monitor

**graphic** **monitor**. The part of the NetView Graphic Monitor Facility that allows the user to select a view. The user can then monitor, browse, edit, or delete a view.

group

**group**. In the NetView/PC program, to identify a set of application programs that are to run concurrently..

<a id="GLS"></a>

H

half-session

**half-session**. In SNA, a component that provides function management data (FMD) services, data flow control, and transmission control for one of the sessions of a network addressable unit (NAU). See also *primary* *half-session* and *secondary* *half-session*.

hard error

**hard** **error**. An error condition on a ring network that requires that the ring be reconfigured or that the source of the error be removed before the ring can resume reliable operation.

hardware monitor

**hardware** **monitor**. The component of the NetView program that helps identify network problems, such as hardware, software, and microcode, from a central control point using interactive display techniques.

held alert

**held** **alert**. An alert that an alert sender was unable to send to a control point immediately. The alert condition it reports may or may not still be impacting availability. See also *delayed* *alert*.

help panel

**help** **panel**. An online display that tells you how to use a command or another aspect of a product. See *task* *panel*.

host

**host**. Synonymous with *host* *processor*.

host LU

**host** **LU**. An SNA logical unit located in a host processor, for example, a VTAM application program. Contrast with *peripheral* *LU*.

host node

**host** **node**. A node providing an application program interface (API) and a common application interface. See *boundary* *node*, *node*, *peripheral* *node* *subarea* *host* *node*, and *subarea* *node*. See also *boundary* *function* and *node* *type*.

host processor

host processor. (1) (TC97) A processor that controls all or part of a user application network. (2) In a network, the processing unit in which

the? data communication access method resides..

<a id="GLS"></a>

I

I-frame

**I-frame**. A DLC frame type for transmitting data. Other DLC frame types are for control, status, and supervisory information.

implicit focal point

**implicit** **focal** **point**. An assigned focal point for which the nodes to be included in its sphere of control are defined at the SOC nodes. The exchange that brings a node into the sphere of control of an implicit focal point is initiated by the SOC node. Contrast with *explicit* *focal* *point.*

IMR

**IMR**. Intensive mode recording.

inactive

**inactive**. Describes the state of a resource that has not been activated or for which the VARY INACT command has been issued. Contrast with *active*. See also *inoperative*.

incoming call packet

**incoming** **call** **packet**. A call supervision packet transmitted by a data circuit-terminating equipment (DCE) to inform a called data terminal equipment (DTE) that another DTE has requested a call.

information (I) format

**information** **(I)** **format**. A format used for information transfer.

inhibited

**inhibited**. In VTAM, pertaining to a logical unit (LU) that has indicated to its system services control point (SSCP) that it is not ready to establish LU-LU sessions. An initiate request for a session with an inhibited LU will be rejected by the SSCP. The LU can separately indicate whether this applies to its ability to act as a primary logical unit (PLU) or as a secondary logical unit (SLU). See also *enabled* and *disabled*.

initial program load (IPL)

**initial** **program** **load** **(IPL)**. (1) The initialization procedure that causes an operating system to commence operation. (2) The process by which a configuration image is loaded into storage at the beginning of a work day or after a system malfunction. (3) The process of loading system programs and preparing a system to run jobs.

inoperative

**inoperative**. The condition of a resource that has been active, but is not. The resource may have failed, received an INOP request, or is suspended while a reactivate command is being processed. See also *inactive*.

input/output channel

**input/output** **channel**. (1) (ISO) In a data processing system, a functional unit that handles the transfer of data between internal and peripheral equipment. (2) In a computing system, a functional unit, controlled by a processor, that handles the transfer of data between processor storage and local peripheral devices. Synonymous with *data* *channel*. See *channel*. See *also* link.

installation exit

**installation** **exit**. The means specifically described in an IBM software product's documentation by which an IBM software product may be modified by a customer's system programmers to change or extend the functions of the IBM software product. Such modifications consist of exit routines written to replace an existing module of an IBM software product, or to add one or more modules or subroutines to an IBM software product for the purpose of modifying (including extending) the functions of the IBM software product. Formerly called *user* *exit*.

intensive mode recording (IMR)

**intensive** **mode** **recording** **(IMR)**. An NCP function that forces recording of temporary errors for a specified resource.

interface

**interface**. A shared boundary. An interface might be a hardware component to link two devices or it might be a portion of storage or registers accessed by two or more computer programs.

IPL

**IPL**. (1) * Initial program loader. (2) Initial program load..

<a id="GLS"></a>

K

kanji

**kanji**. An ideographic character set used in Japanese. See also *double-byte* *character* *set*.

keyword

**keyword**. (1) (TC97) A lexical unit that, in certain contexts, characterizes some language construction. (2) One of the predefined words of an artificial language. (3) One of the significant and informative words in a title or document that describes the content of that document. (4) A name or symbol that identifies a parameter. (5) A part of a command operand that consists of a specific character string (such as DSNAME=). See also *definition* *statement* and *keyword* *operand*. Contrast with *positional* *operand*.

keyword operand

**keyword** **operand**. An operand that consists of a keyword followed by one or more values (such as DSNAME=HELLO). See also *definition* *statement*. Contrast with *positional* *operand*.

keyword parameter

**keyword** **parameter**. A parameter that consists of a keyword followed by one or more values..

<a id="GLS"></a>

L

LAN

**LAN**. Local area network.

line

**line**. See *communication* *line*.

line control

**line** **control**. Synonym for *data* *link* *control* *protocol*.

line control discipline

**line** **control** **discipline**. Synonym for *link* *protocol*.

line discipline

**line** **discipline**. Synonym for *link* *protocol*.

line switching

**line** **switching**. *Synonym* for circuit *switching*.

link

**link**. (1) In SNA, the combination of the link connection and the link stations joining network nodes, for example a System/370 channel and its associated protocols or a serial-by-bit connection under the control of Synchronous Data Link Control (SDLC). A link connection is the physical medium of transmission. A link, however, is both logical and physical.

Synonymous with data link. (2) In the NetView Graphic Monitor Facility, the graphical element of a view that represents a connection between two nodes. A link represents one or more of the following SNA objects or

aggregate resources: SNA? transmission groups SDLC links that connect communication controllers with each other SDLC links that connect communication controllers with cluster controllers and programmable workstations Channels (a type of SNA transmission group) SNA link components SNA link stations Cluster connections. See *also* *real* link and *aggregate* *link.*

link connection

**link** **connection**. In SNA, the physical equipment providing two-way communication between one link station and one or more other link

for example, a telecommunication line and data circuit terminating equipment (DCE).

link connection segment

**link** **connection** **segment**. A portion of the configuration that is located between two resources listed consecutively in the service point command service (SPCS) query link configuration request list.

link level

**link** **level**. (1) A part of Recommendation X.25 that defines the link protocol used to get data into and out of the network across the duplex link connecting the subscriber's machine to the network node. LAP and LAPB are the link access protocols recommended by the CCITT. (2) See *data* *link* *level*.

link problem determination aid (LPDA)

**link** **problem** **determination** **aid** **(LPDA)**. A series of testing procedures initiated by the NetView program, VTAM, or NCP that provide modem status, attached device status, and the overall quality of a communications link.

link protocol

**link** **protocol**. (1) See *protocol*. (2) See *also* link *level*.

link-attached

**link-attached**. Pertaining to devices that are physically connected by a telecommunication line. Contrast with *channel-attached*. Synonymous with *remote*.

lobe

**lobe**. In the IBM Token-Ring Network, the section of cable (which may consist of several segments) that connects a device to an access unit.

local

**local**. Pertaining to a device that is attached to a controlling unit by cables, rather than by a telecommunication line. Synonymous with *channel-attached*.

local address

**local** **address**. In SNA, an address used in a peripheral node in place of an SNA network address and transformed to or from an SNA network address by the boundary function in a subarea node.

local area network (LAN)

**local** **area** **network** **(LAN)**. (1) A network in which a set of devices are connected to one another for communication and that can be connected to a larger network. See also *token* *ring*. (2) A network in which communications are limited to a moderately sized geographic area such as a extend across public rights-of-way. Contrast with *wide* *area* *network*.

logged-on operator

**logged-on** **operator**. A NetView operator station task that requires a terminal and a logged-on user. Contrast with *autotask*.

logical channel

**logical** **channel**. In packet mode operation, a sending channel and a receiving channel that together are used to send and receive data over a data link at the same time. Several logical channels can be established on the same data link by interleaving the transmission of packets.

logical channel identifier

**logical** **channel** **identifier**. A bit string in the header of a packet that associates the packet with a specific switched virtual circuit or permanent virtual circuit.

logical unit (LU)

**logical** **unit** **(LU)**. In SNA, a port through which an end user accesses the SNA network and the functions provided by system services control points (SSCPs). An LU can support at least two sessions--one with an SSCP and one with another LU--and may be capable of supporting many sessions with other LUs. See also *network* *addressable* *unit* *(NAU)*, *peripheral* *LU* *physical* *unit* *(PU)*, *system* *services* *control* *point* *(SSCP)*, *primary* *logical* *unit* *(PLU)*, and *secondary* *logical* *unit* *(SLU)*.

logical unit (LU) services

**logical** **unit** **(LU)** **services**. In SNA, capabilities in a logical unit to: (1) receive requests from an end user and, in turn, issue requests to the system services control point (SSCP) in order to perform the requested functions, typically for session initiation; (2) receive requests from the SSCP, for example to activate LU-LU sessions via Bind Session requests; and (3) provide session presentation and other services for LU-LU sessions. See also *physical* *unit* *(PU)* *services*.

logical unit (LU) 6.2

**logical** **unit** **(LU)** **6.2**. A type of logical unit that supports general communication between programs in a distributed processing environment. LU 6.2 is characterized by (1) a peer relationship between session partners, (2) efficient utilization of a session for multiple transactions, (3) comprehensive end-to-end error processing, and (4) a generic application program interface (API) consisting of structured verbs that are mapped into a product implementation.

loop adapter

**loop** **adapter**. A feature of the IBM 4300 Processor family that allows the attachment of a variety of SNA and non-SNA devices. To VTAM, these devices appear as channel-attached type 2 physical units (PUs).

LPDA

**LPDA**. Link Problem Determination Aid.

LU

**LU**. Logical unit.

LU type

**LU** **type**. In SNA, the classification of an LU-LU session in terms of the specific subset of SNA protocols and options supported by the logical units (LUs) for that session, namely: The mandatory and optional values allowed in the session activation request. The usage of data stream controls, function management headers (FMHs), request unit (RU) parameters, and sense codes. Presentation services protocols such as those associated with FMH usage. LU types 0, 1, 2, 3, 4, 6.1, 6.2, and 7 are defined.

LU 6.2

**LU** **6.2**. Logical unit 6.2.

LU-LU session

**LU-LU** **session**. In SNA, a session between two logical units (LUs) in an SNA network. It provides communication between two end users, or between an end user and an LU services component.

LU-LU session type

**LU-LU** **session** **type**. A deprecated term for *LU* *type*..

<a id="GLS"></a>

M

macroinstruction

**macroinstruction**. (1) An instruction that when executed causes the execution of a predefined sequence of instructions in the same source language. (2) In assembler programming, an assembler language statement that causes the assembler to process a predefined set of statements called a macro definition. The statements normally produced from the macro definition replace the macroinstruction in the program. See also *definition* *statement*.

maintenance and operator subsystem (MOSS)

**maintenance** **and** **operator** **subsystem** **(MOSS)**. A subsystem of an IBM communication controller, such as the 3725 or the 3720, that contains a processor and operates independently of the rest of the controller. It loads and supervises the controller, runs problem determination procedures, and assists in maintaining both hardware and software. See also *configuration* *services,* *management* *services,* *network*.

major node

**major** **node**. In VTAM, a set of resources that can be activated and deactivated as a group. See *node* and *minor* *node*. See also *configuration* *services,* *maintenance* *services,* *network*.

management services unit (MSU)

**management** **services** **unit** **(MSU)**. A generic term for network management data, regardless of the encoding used to transport the data. It includes the network management vector transport (NMVT), control point management services unit (CP-MSU), and multiple-domain support message unit (MDS-MU).

message

**message**. (1) (TC97) A group of characters and control bit sequences transferred as an entity. (2) In VTAM, the amount of function management data (FMD) transferred to VTAM by the application program with one SEND request.

message switching

**message** **switching**. (1) * (ISO) In a data network, the process of routing messages by receiving, storing, and forwarding complete messages. (2) The technique of receiving a complete message, storing, and then forwarding it unaltered to its destination.

migration

**migration**. Installing a new version or release of a program when an earlier version or release is already in place.

minor node

**minor** **node**. In VTAM, a uniquely-defined resource within a major node. See *node* and *major* *node*.

modem

**modem**. A device that modulates and demodulates signals transmitted over data communication facilities. The term is a contraction for modulator-demodulator.

module

**module**. A program unit that is discrete and identifiable with respect to compiling, combining with other units, and loading; for example, the input to or output from an assembler, compiler, linkage editor, or executive routine.

monitor

**monitor**. (1) In the IBM Token-Ring Network, the function required to initiate the transmission of a token on the ring and to provide soft-error recovery in case of lost tokens, circulating frames, or other difficulties. The capability is present in all ring stations. (2) In the NetView Graphic Monitor Facility, to look at a view that changes when status changes. Contrast with *browse*.

MOSS

**MOSS**. Maintenance and operator subsystem.

MSU

**MSU**. management services unit

Multiple Virtual Storage (MVS)

**Multiple** **Virtual** **Storage** **(MVS)**. An IBM licensed program whose full name is the Operating System/Virtual Storage (OS/VS) with Multiple Virtual Storage/System Product for System/370. It is a software operating system controlling the execution of programs.

Multiple Virtual Storage for Extended Architecture (MVS/XA)

**Multiple** **Virtual** **Storage** **for** **Extended** **Architecture** **(MVS/XA)**. An IBM licensed program whose full name is the Operating System/Virtual Storage (OS/VS) with Multiple Virtual Storage/System Product for Extended Architecture. Extended architecture allows 31-bit storage addressing. MVS/XA is a software operating system controlling the execution of programs.

multiple-domain network

**multiple-domain** **network**. In SNA, a network with more than one system services control point (SSCP). Contrast with *single-domain* *network*.

MVS

**MVS**. Multiple Virtual Storage.

MVS/ESA

**MVS/ESA**. Multiple Virtual Storage/Enterprise Systems Architecture.

MVS/XA

**MVS/XA**. Multiple Virtual Storage for Extended Architecture..

<a id="GLS"></a>

N

NAU

**NAU**. Network addressable unit.

NCCF

**NCCF**. A command that starts the NetView command facility. NCCF also identifies various panels and functions as part of the command facility.

NCP

**NCP**. (1) Network Control Program (IBM licensed program). Its full name is Advanced Communications Function for the Network Control Program. Synonymous with *ACF/NCP*. (2) Network control program (general term).

NCP/Token-Ring interconnection (NTRI)

**NCP/Token-Ring** **interconnection** **(NTRI)**. An NCP function that allows a communication controller to attach to the IBM Token-Ring Network and provides both subarea and peripheral node DLC services in the SNA network.

negative response (NR)

**negative** **response** **(NR)**. In SNA, a response indicating that a request did not arrive successfully or was not processed successfully by the receiver. Contrast with *positive* *response*. See *exception* *response*.

NetView

**NetView**. A system 370-based IBM licensed program used to monitor a network, manage it, and diagnose its problems.

NetView Graphic Monitor Facility

**NetView** **Graphic** **Monitor** **Facility**. A function of the NetView program that provides a graphic topological presentation of a network that is controlled by the NetView program. It provides to the operator different views of a network, multiple levels of graphical detail, and dynamic resource status of the network.

NetView-NetView task (NNT)

**NetView-NetView** **task** **(NNT)**. The task under which a cross-domain NetView operator session runs. See *operator* *station* *task*.

NetView/PC

**NetView/PC**. A PC-based IBM licensed program through which application programs can be used to monitor, manage, and diagnose problems in IBM Token-Ring networks, non-SNA communication devices, and voice networks.

network

**network**. (1) (TC97) An interconnected group of nodes. (2) In data processing, a user application network. See *path* *control* *network,* *public* *network,* *SNA* *network*, and *user-application* *network*.

network address

**network** **address**. In SNA, an address, consisting of subarea and element fields, that identifies a link, a link station, or a network addressable unit. Subarea nodes use network addresses; peripheral nodes use local addresses. The boundary function in the subarea node to which a peripheral node is attached transforms local addresses to network addresses and vice versa. See *local* *address*. See also *network* *name*.

network addressable unit (NAU)

**network** **addressable** **unit** **(NAU)**. In SNA, a logical unit, a physical unit, or a system services control point. It is the origin or the destination of information transmitted by the path control network. Each NAU has a network address that represents it to the path control network. See also *network* *name*, *network* *address*, and *path* *control* *network*.

network control program

**network** **control** **program**. A program, generated by the user from a library of IBM-supplied modules, that controls the operation of a communication controller.

Network Control Program (NCP)

**Network** **Control** **Program** **(NCP)**. An IBM licensed program that provides communication controller support for single-domain, multiple-domain, and interconnected network capability. Its full name is Advanced Communications Function for the Network Control Program.

network controller

**network** **controller**. A concentrator and protocol converter used with SDLC links. By converting protocols, which manage the way data is sent and received, the IBM 3710 Network Controller allows the use of non-SNA devices with an SNA host processor.

network identifier (network ID)

network identifier (network ID). The network name defined to NCPs and hosts to indicate the name of the network in which they reside. It is

unique? across all communicating SNA networks.

network manager

**network** **manager**. A program or group of programs that is used to monitor, manage, and diagnose the problems of a network.

network name

**network** **name**. (1) In SNA, the symbolic identifier by which end users refer to a network addressable unit (NAU), a link, or a link station. See also *network* *address*. (2) In a multiple-domain network, the name of the APPL statement defining a VTAM application program is its network name and it must be unique across domains. Contrast with *ACB* *name*. See *uninterpreted* *name*.

network operator

**network** **operator**. (1) A person or program responsible for controlling the operation of all or part of a network. (2) The person or program that controls all the domains in a multiple-domain network. Contrast with *domain* *operator*.

Network Routing Facility (NRF)

**Network** **Routing** **Facility** **(NRF)**. An IBM licensed program that resides in the NCP, which provides a path for messages between terminals, and routes messages over this path without going through the host processor. See *configuration* *services,* *maintenance* *services,* *management*.

no response

**no** **response**. In SNA, a value in the form-of-response-requested field of the request header (RH) indicating that no response is to be returned to the request, whether or not the request is received and processed successfully. Contrast with *definite* *response* and *exception* *response*.

node

**node**. (1) In SNA, an endpoint of a link or junction common to two or more links in a network. Nodes can be distributed to host processors, communication controllers, cluster controllers, or terminals. Nodes can vary in routing and other functional capabilities. See *boundary* *node* *host* *node*, *peripheral* *node*, and *subarea* *node*. (2) In VTAM, a point in a

network defined by a symbolic name. See major node and minor node. (3) In the NetView Graphic Monitor Facility, the graphical element of a

view? that represents one or more: Cluster nodes Subarea nodes Composite nodes Peripheral nodes Null nodes If a node represents more than one resource, the resources represented may be of different types. See also *real* *node* and *aggregate* *node.*

node type

**node** **type**. In SNA, a designation of a node according to the protocols it supports and the network addressable units (NAUs) that it can contain. Five types are defined: 1, 2.0, 2.1, 4, and 5. Type 1, type 2.0, and type 2.1 nodes are peripheral nodes; type 4 and type 5 nodes are subarea nodes. See *also* type *2.1* *node*.

Non-SNA Interconnection (NSI)

**Non-SNA** **Interconnection** **(NSI)**. An IBM licensed program that provides format identification (FID1/4) support for selected non-SNA facilities. Thus, it allows SNA and non-SNA facilities to share SDLC links. It also allows the remote concentration of selected non-SNA devices along with SNA devices.

NPSI

**NPSI**. X.25 NCP Packet Switching Interface.

NR

**NR**. Negative response.

NRF

**NRF**. Network Routing Facility.

NSI

**NSI**. Non-SNA Interconnection.

NTRI

**NTRI**. NCP/Token-Ring interconnection..

<a id="GLS"></a>

O

online

**online**. Stored in a computer and accessible from a terminal.

open

**open**. (1) In the IBM Token-Ring Network, to make an adapter ready for use. (2) A break in an electrical circuit.

operand

**operand**. (1) (ISO) An entity on which an operation is performed. (2) That which is operated upon. An operand is usually identified by an address part of an instruction. (3) Information entered with a command name to define the data on which a command processor operates and to control the execution of the command processor. (4) An expression to whose value an operator is applied. See also *definition* *statement*, *keyword*, *keyword* *parameter*, and *parameter*.

operator

**operator**. (1) In a language statement, the lexical entity that indicates the action to be performed on operands. See also *definition* *statement*. (2) A person who operates a machine. See *network* *operator*. (3) A person or program responsible for managing activities controlled by a given piece of software such as MVS, the NetView program, or IMS. See *logged-on* *operator* and *network* *operator*. See also *autotask* and *operator* *station* *task*.

operator station task (OST)

**operator** **station** **task** **(OST)**. The NetView task that establishes and maintains the online session with the network operator. There is one operator station task for each network operator who logs on to the NetView program. See *NetView-NetView* *task*.

OS/2

**OS/2**. An operating system that runs on a personal computer with a 80286 processor or larger (for example, an IBM Personal System/2 model 50)..

<a id="GLS"></a>

P

pacing group

**pacing** **group**. In SNA, (1) The path information units (PIUs) that can be transmitted on a virtual route before a virtual-route pacing response is received, indicating that the virtual route receiver is ready to receive more PIUs on the route. Synonymous with *window*. (2) The requests that can be transmitted on the normal flow in one direction on a session before a session-level pacing response is received, indicating that the receiver is ready to accept the next group of requests.

packet

**packet**. (ISO) A sequence of binary digits, including data and control signals, that is transmitted and switched as a composite whole. The data, control signals, and possibly error control information are arranged in a specific format. See *call-accepted* *packet*, *call-connected* *packet*, *call* *request* *packet*, *call* *supervision* *packets*, *clear* *indication* *packet*, *clear* *request* *packet*, *data* *packet*, *DCE* *clear* *confirmation* *packet*, *discarded* *packet*, *incoming* *call* *packet*, *permit* *packet*, and *reset* *packet*.

packet level

**packet** **level**. (1) The packet format and control procedures for exchange of packets containing control information and user data between data terminal equipment (DTE) and data circuit-terminating equipment (DCE). See also *data* *link* *level* and *physical* *level*. (2) A part of Recommendation X.25 that defines the protocol for establishing logical connections between two DTEs and for transferring data on these connections.

packet mode operation

**packet** **mode** **operation**. Synonym for *packet* *switching*.

packet switching

**packet** **switching**. (1) (ISO) The process of routing and transferring data by means of addressed packets so that a channel is occupied only during the transmission of a packet. On completion of the transmission, the channel is made available for the transfer of other packets. (2) Synonymous with *packet* *mode* *operation*. See also *circuit* *switching*.

page

**page**. (1) The portion of a panel that is shown on a display surface at one time. (2) To move back and forth among the pages of a multiple-page panel. See also *scroll*. (3) (ISO) In a virtual storage system, a fixed-length block that has a virtual address and that can be transferred between real storage and auxiliary storage. (4) To transfer instructions, data, or both between real storage and external page or auxiliary storage.

panel

**panel**. (1) A formatted display of information that appears on a terminal screen. See *help* *panel* and *task* *panel*. Contrast with *screen*. (2) In computer graphics, a display image that defines the locations and characteristics of display fields on a display surface.

parameter

**parameter**. (1) (ISO) A variable that is given a constant value for a specified application and that may denote the application. (2) An item in a menu for which the user specifies a value or for which the system provides a value when the menu is interpreted. (3) Data passed to a program or procedure by a user or another program, namely as an operand in a language statement, as an item in a menu, or as a shared data structure. See also *keyword*, *keyword* *parameter*, and *operand*.

path control (PC)

**path** **control** **(PC)**. The function that routes message units between network addressable units (NAUs) in the network and provides the paths between them. It converts the BIUs from transmission control (possibly segmenting them) into path information units (PIUs) and exchanges basic transmission units (BTUs) and one or more PIUs with data link control. Path control differs for peripheral nodes, which use local addresses for routing, and subarea nodes, which use network addresses for routing. See *peripheral* *path* *control* and *subarea* *path* *control*. See *also* link, *peripheral* *node*, and *subarea* *node*.

path control (PC) layer

**path** **control** **(PC)** **layer**. In SNA, the layer that manages the sharing of link resources of the SNA network and routes basic information units (BIUs) through it. See also *BIU* *segment*, *blocking* *of* *PIUs*, *data* *link* *control* *layer*, and *transmission* *control* *layer*.

path control (PC) network

**path** **control** **(PC)** **network**. In SNA, the part of the SNA network that includes the data link control and path control layers. See *SNA* *network* and *user* *application* *network*. See also *boundary* *function*.

PC

**PC**. (1) Path control. (2) Personal Computer. Its full name is the IBM Personal Computer.

performance error

**performance** **error**. Synonym for *temporary* *error*.

peripheral host node

**peripheral** **host** **node**. A node that provides an application program interface (API) for running application programs but does not provide SSCP functions and is not aware of the network configuration. The peripheral host node does not provide subarea node services. It has boundary function provided by its adjacent subarea. See *boundary* *node*, *host* *node* *node*, *peripheral* *node*, *subarea* *host* *node*, and *subarea* *node*. See also *boundary* *function* and *node* *type*.

peripheral LU

**peripheral** **LU**. Peripheral logical unit.

peripheral node

**peripheral** **node**. A node that uses local addresses for routing and therefore is not affected by changes in network addresses. A peripheral node requires boundary-function assistance from an adjacent subarea node. A peripheral node is a physical unit (PU) type 1, 2.0, or 2.1 node connected to a subarea node with boundary function within a subarea. See <> *boundary* *node*, *host* *node*, *node*, *peripheral* *host* *node*, *subarea* *host* *node*, and *subarea* *node*. See also *boundary* *function* and *node* *type*.

peripheral path control

**peripheral** **path** **control**. The function in a peripheral node that routes message units between units with local addresses and provides the paths between them. See *path* *control* and *subarea* *path* *control*. See also *boundary* *function*, *peripheral* *node*, and *subarea* *node*.

peripheral PU

**peripheral** **PU**. Peripheral physical unit.

permanent error

**permanent** **error**. A resource error that cannot be resolved by error recovery *programs.* Contrast with temporary *error*.

permanent virtual circuit (PVC)

**permanent** **virtual** **circuit** **(PVC)**. A virtual circuit that has a logical channel permanently assigned to it at each data terminal equipment (DTE). A call establishment protocol is not required.

permit packet

**permit** **packet**. At the interface between a data terminal equipment (DTE) and a data circuit-terminating equipment (DCE), a packet used to transmit permits over a virtual circuit.

Personal Computer (PC)

**Personal** **Computer** **(PC)**. The IBM Personal Computer line of products including the 5150 and subsequent models.

physical circuit

**physical** **circuit**. A circuit established without multiplexing. Contrast with *virtual* *circuit*. See *also* data *circuit*.

physical connection

**physical** **connection**. In VTAM, a point-to-point connection or multipoint connection. Synonymous with *connection*.

physical level

**physical** **level**. The mechanical, electrical, functional, and procedural media used to activate, maintain, and deactivate the physical link between the data terminal equipment (DTE) and the data circuit-terminating equipment (DCE). See also *data* *link* *level* and *packet* *level*.

physical unit (PU)

**physical** **unit** **(PU)**. In SNA, a type of network addressable unit (NAU). A physical unit (PU) manages and monitors the resources (such as attached links) of a node, as requested by a system services control point (SSCP) through an SSCP-PU session. An SSCP activates a session with the physical unit in order to indirectly manage, through the PU, resources of the node such as attached links. See also *peripheral* *PU* and *subarea* *PU*.

physical unit (PU) services

**physical** **unit** **(PU)** **services**. In SNA, the components within a physical unit (PU) that provide configuration services and maintenance services for SSCP-PU sessions. See also *logical* *unit* *(LU)* *services*.

PLU

**PLU**. Primary logical unit.

positional operand

**positional** **operand**. An operand in a language statement that has a fixed position. See also *definition* *statement*. Contrast with *keyword* *operand*.

positive response

**positive** **response**. A response indicating that a request was received and processed. Contrast with *negative* *response*.

POST

**POST**. Power-on self test. A series of diagnostic tests that are run each time the computer's power is turned on.

primary half-session

**primary** **half-session**. In SNA, the half-session that sends the session activation request. See also *primary* *logical* *unit*. Contrast with *secondary* *half-session*.

primary logical unit (PLU)

**primary** **logical** **unit** **(PLU)**. In SNA, the logical unit (LU) that contains the primary half-session for a particular LU-LU session. Each session must have a PLU and secondary logical unit (SLU). The PLU is the unit responsible for the bind and is the controlling LU for the session. A particular LU may contain both primary and secondary half-sessions for different active LU-LU sessions. Contrast with *secondary* *logical* *unit* *(SLU)*.

problem determination

**problem** **determination**. The process of identifying the source of a problem; for example, a program component, a machine failure, telecommunication facilities, user or contractor-installed programs or equipment, an environment failure such as a power loss, or a user error.

protocol

**protocol**. (1) (CCITT/ITU) A specification for the format and relative timing of information exchanged between communicating parties. (2) (TC97) The set of rules governing the operation of functional units of a communication system that must be followed if communication is to be achieved. (3) In SNA, the meanings of, and the sequencing rules for, requests and responses used for managing the network, transferring data, and synchronizing the states of network components. Synonymous with *line* *control* *discipline* and *line* *discipline*. See also *bracket* *protocol* and *link* *protocol*.

PU

**PU**. Physical unit.

PU-PU flow

**PU-PU** **flow**. In SNA, the exchange between physical units (PUs) of network control requests and responses.

public network

**public** **network**. A network established and operated by communication common carriers or telecommunication Administrations for the specific purpose of providing circuit-switched, packet switched, and leased-circuit services to the public. Contrast with *user-application* *network*..

<a id="GLS"></a>

R

real link

**real** **link**. In the NetView Graphic Monitor Facility, a link that represents an actual resource. The only real link is the link component in the sub-link component view. See also *link* and *aggregate* *link.*

real node

**real** **node**. In the NetView Graphic Monitor Facility, a node that represents an actual resource (other than a link station or link component) that is being monitored, such as a host or a communication controller. See *also* node and *aggregate* *node.*

Recommendation X.21

**Recommendation** **X.21**. A Consultative Committee on International Telegraph and Telephone (CCITT) recommendation for a general purpose interface between data terminal equipment and data circuit equipment for synchronous operations on a public data network.

Recommendation X.25

**Recommendation** **X.25**. A Consultative Committee on International Telegraph and Telephone (CCITT) recommendation for the interface between data terminal equipment and packet-switched data networks. See also *packet* *switching*.

record

**record**. (1) (ISO) In programming languages, an aggregate that consists of data objects, possibly with different attributes, that usually have identifiers attached to them. In some programming languages, records are called structures. (2) (TC97) A set of data treated as a unit. (3) A set of one or more related data items grouped for processing. (4) In VTAM, the unit of data transmission for record mode. A record represents whatever amount of data the transmitting node chooses to send.

release

**release**. For VTAM, to relinquish control of resources (communication controllers or physical units). See also *resource* *takeover*. Contrast with *acquire* *(2)*.

remote

**remote**. Concerning the peripheral parts of a network not centrally linked to the host processor and generally using telecommunication lines with public right-of-way.

remove

**remove**. In the IBM Token-Ring Network, to take an attaching device off the ring.

request unit (RU)

**request** **unit** **(RU)**. In SNA, a message unit that contains control information, end-user data, or both.

request/response unit (RU)

**request/response** **unit** **(RU)**. In SNA, a generic term for a request unit or a response unit. See also *request* *unit* *(RU)* and *response* *unit*.

reset

**reset**. On a virtual circuit, reinitialization of data flow control. At reset, all data in transit are eliminated.

reset packet

**reset** **packet**. A packet used to reset a virtual circuit at the interface between the data terminal equipment (DTE) and the data circuit-terminating equipment (DCE).

resource

**resource**. (1) Any facility of the computing system or operating system required by a job or task, and including main storage, input/output devices, the processing unit, data sets, and control or processing programs. (2) In the NetView program, any hardware or software that provides function to the network. (3) In the NetView Graphic Monitor Facility. a real or aggregate link or node in a view.

resource takeover

**resource** **takeover**. In VTAM, action initiated by a network operator to transfer control of resources from one domain to another. See also *acquire* *(2)* and *release*. See *takeover*.

response

**response**. A reply represented in the control field of a response frame. It advises the primary or combined station of the action taken by the secondary or other combined station to one or more commands. See also *command*.

response time

**response** **time**. (1) The amount of time it takes after a user presses the enter key at the terminal until the reply appears at the terminal. (2) For response time monitoring, the time from the activation of a transaction until a response is received, according to the response time definition coded in the performance class.

response time monitor (RTM)

**response** **time** **monitor** **(RTM)**. A feature available with certain hardware devices to allow measurement of response times, which may be collected and displayed by the NetView program.

response unit (RU)

**response** **unit** **(RU)**. In SNA, a message unit that acknowledges a request unit; it may contain prefix information received in a request unit. If positive, the response unit may contain additional information (such as session parameters in response to Bind Session), or if negative, contains sense data defining the exception condition.

ring

**ring**. A network configuration where a series of attaching devices are connected by unidirectional transmission links to form a closed path.

routing

**routing**. The assignment of the path by which a message will reach its destination.

Routing and targeting instructions (R&TI)

**Routing** **and** **targeting** **instructions** **(R&TI)**. A GDS variable that may be present in the CPM-SUs that flow for remote operations.

RTM

**RTM**. Response time monitor.

RU

**RU**. Request/response unit.

RU chain

**RU** **chain**. In SNA, a set of related request/response units (RUs) that are consecutively transmitted on a particular normal or expedited data flow. The request RU chain is the unit of recovery: if one of the RUs in the chain cannot be processed, the entire chain is discarded. Each RU belongs to only one chain, which has a beginning and an end indicated by means of control bits in request/response headers within the RU chain. Each RU can <> be designated as first-in-chain (FIC), last-in-chain (LIC), (Resource: middle-in-chain (MIC), or only-in-chain (OIC). Response units and expedited-flow request units are always sent as only-in-chain.

R&TI

**R&TI**. Routing and targeting instructions.

<a id="GLS"></a>

S

scanner

**scanner**. (1) A device capable of electronically reviewing amounts of data and translating the data into a machine readable form. (2) For the 3725 communication controller, a processor dedicated to controlling a small number of telecommunication lines. It provides the connection between the line interface coupler hardware and the central control unit.

screen

**screen**. An illuminated display surface; for example, the display surface of a CRT or plasma panel. Contrast with *panel*.

scroll

**scroll**. To move all or part of the display image vertically to display data that cannot be observed within a single display image. See also *page* *(2)*.

SDLC

**SDLC**. Synchronous Data Link Control.

secondary half-session

**secondary** **half-session**. In SNA, the half-session that receives the session-activation request. See also *secondary* *logical* *unit* *(SLU)*. Contrast with *primary* *half-session*.

secondary logical unit (SLU)

**secondary** **logical** **unit** **(SLU)**. In SNA, the logical unit (LU) that contains the secondary half-session for a particular LU-LU session. An LU may contain secondary and primary half-sessions for different active LU-LU sessions. Contrast with *primary* *logical* *unit* *(PLU)*.

secondary logical unit (SLU) key

**secondary** **logical** **unit** **(SLU)** **key**. A key-encrypting key used to protect a session cryptography key during its transmission to the secondary half-session.

segment

**segment**. (1) In the IBM Token-Ring Network, a section of cable between components or devices on the network. A segment may consist of a single patch cable, multiple patch cables connected together, or a combination of building cable and patch cables connected together. (2) See *link* *connection* *segment*.

sequence number

sequence number. A number assigned to a particular frame or packet to

control? the transmission flow and receipt of data.

service point (SP)

**service** **point** **(SP)**. An entry point that supports applications that provide network management for resources not under the direct control of itself as an entry point. Each resource is either under the direct control of another entry point or not under the direct control of any entry point. A service point accessing these resources is not required to use SNA sessions (unlike a focal point). A service point is needed when entry point support is not yet available for some network management function.

session

**session**. In SNA, a logical connection between two network addressable units (NAUs) that can be activated, tailored to provide various protocols, and deactivated, as requested. Each session is uniquely identified in a transmission header (TH) by a pair of network addresses, identifying the origin and destination NAUs of any transmissions exchanged during the session. See *half-session*, *LU-LU* *session*, *SSCP-LU* *session*, *SSCP-PU* *session*, and *SSCP-SSCP* *session*. See also *LU-LU* *session* *type* and *PU-PU* *flow*.

session partner

**session** **partner**. In SNA, one of the two network addressable units (NAUs) having an active session. See *configuration* *services*,

shared

**shared**. Pertaining to the availability of a resource to more than one use at the same time.

single-domain network

**single-domain** **network**. In SNA, a network with one system services control point (SSCP). Contrast with *multiple-domain* *network*.

SLU

**SLU**. Secondary logical unit.

SNA

**SNA**. Systems Network Architecture.

SNA network

**SNA** **network**. The part of a user-application network that conforms to the formats and protocols of Systems Network Architecture. It enables reliable transfer of data among end users and provides protocols for controlling the resources of various network configurations. The SNA network consists of network addressable units (NAUs), boundary function components, and the path control network.

SP

**SP**. Service point.

SSCP

**SSCP**. System services control point.

SSCP-LU session

**SSCP-LU** **session**. In SNA, a session between a system services control point (SSCP) and a logical unit (LU); the session enables the LU to request the SSCP to help initiate LU-LU sessions.

SSCP-PU session

**SSCP-PU** **session**. In SNA, a session between a system services control point (SSCP) and a physical unit (PU); SSCP-PU sessions allow SSCPs to send requests to and receive status information from individual nodes in order to control the network configuration.

SSCP-SSCP session

**SSCP-SSCP** **session**. In SNA, a session between the system services control point (SSCP) in one domain and the SSCP in another domain. An SSCP-SSCP session is used to initiate and terminate cross-domain LU-LU sessions.

SSP

**SSP**. System Support Programs (IBM licensed program). Its full name is Advanced Communications Function for System Support Programs. Synonymous with *ACF/SSP*.

statement

**statement**. A language syntactic unit consisting of an operator, or other statement identifier, followed by one or more operands. See *definition* *statement*.

station

**station**. (1) One of the input or output points of a network that uses communication facilities; for example, the telephone set in the telephone system or the point where the business machine interfaces with the channel on a leased private line. (2) One or more computers, terminals, or devices at a particular location.

status

**status**. The measure of the condition or availability of a resource.

status code

**status** **code**. In VTAM, information on the status of a resource as shown in a 10-character state code; for example, STATEACTIV for active.

subarea host node

**subarea** **host** **node**. A host node that provides both subarea function and an application program interface (API) for running application programs. It provides system services control point (SSCP) functions, subarea node services, and is aware of the network configuration. See *boundary* *node* *communication* *management* *configuration* *host* *node*, *data* *host* *node*, *host* *node*, *node*, *peripheral* *node*, and *subarea* *node*. See also *boundary* *function* and *node* *type*.

subarea node

**subarea** **node**. In SNA, a node that uses network addresses for routing and whose routing tables are therefore affected by changes in the configuration of the network. Subarea nodes can provide gateway function, and boundary function support for peripheral nodes. Type 4 and type 5 nodes are subarea nodes. See *boundary* *node*, *host* *node*, *node*, *peripheral* *node*, and *subarea* *host* *node*. See also *boundary* *function* and *node* *type*.

subarea path control

**subarea** **path** **control**. The function in a subarea node that routes message units between network addressable units (NAUs) and provides the paths between them. See *path* *control* and *peripheral* *path* *control*. See also *boundary* *function*, *peripheral* *node*, and *subarea* *node*.

subarea PU

**subarea** **PU**. In SNA, a physical unit (PU) in a subarea node.

subsystem

**subsystem**. A secondary or subordinate system, usually capable of operating independent of, or asynchronously with, a controlling system.

switched network

switched network. Any network in which connections are established by

closing? switches, for example, by dialing.

switched virtual circuit (SVC)

**switched** **virtual** **circuit** **(SVC)**. An X.25 circuit that is dynamically established when needed. The X.25 equivalent of a switched line.

Synchronous Data Link Control (SDLC)

**Synchronous** **Data** **Link** **Control** **(SDLC)**. A discipline for managing synchronous, code-transparent, serial-by-bit information transfer over a link connection. Transmission exchanges may be duplex or half-duplex over switched or nonswitched links. The configuration of the link connection may be point-to-point, multipoint, or loop. SDLC conforms to subsets of the Advanced Data Communication Control Procedures (ADCCP) of the American National Standards Institute and High-Level Data Link Control (HDLC) of the International Standards Organization.

system services control point (SSCP)

**system** **services** **control** **point** **(SSCP)**. In SNA, a central location point within an SNA network for managing the configuration, coordinating network operator and problem determination requests, and providing directory support and other session services for end users of the network. Multiple SSCPs, cooperating as peers, can divide the network into domains of control, with each SSCP having a hierarchical control relationship to the physical units and logical units within its domain.

system services control point (SSCP) domain

system services control point (SSCP) domain. The system services control point and the physical units (PUs), logical units (LUs), links, link stations and all the resources that the SSCP has the ability to control by

means? of activation requests and deactivation requests.

System Support Programs (SSP)

**System** **Support** **Programs** **(SSP)**. An IBM licensed program, made up of a collection of utilities and small programs, that supports the operation of the NCP.

Systems Network Architecture (SNA)

**Systems** **Network** **Architecture** **(SNA)**. The description of the logical structure, formats, protocols, and operational sequences for transmitting information units through and controlling the configuration and operation of networks..

<a id="GLS"></a>

T

takeover

**takeover**. The process by which the failing active subsystem is released from its extended recovery facility (XRF) sessions with terminal users and replaced by an alternate subsystem. See *resource* *takeover*.

task

**task**. A basic unit of work to be accomplished by a computer. The task is usually specified to a control program in a multiprogramming or multiprocessing environment.

task panel

**task** **panel**. Online display from which you communicate with the program in order to accomplish the program's function, either by selecting an option provided on the panel or by entering an explicit command. See *help* *panel*.

TCU

**TCU**. Transmission control unit.

telecommunication line

**telecommunication** **line**. Any physical medium such as a wire or microwave beam, that is used to transmit data. Synonymous with *transmission* *line*.

temporary error

**temporary** **error**. A resource failure that can be resolved by error recovery programs. Synonymous with *performance* *error*. Contrast with *permanent* *error*.

terminal

**terminal**. A device that is capable of sending and receiving information over a link; it is usually equipped with a keyboard and some kind of display, such as a screen or a printer.

threshold

**threshold**. In the NetView program, refers to a percentage value set for a resource and compared to a calculated error-to-traffic ratio.

threshold

**threshold**. In NPM, high or low values supplied by the user to monitor data and statistics being collected.

TIC

**TIC**. Token-ring interface coupler.

time-out

**time-out**. (1) (ISO) An event that occurs at the end of a predetermined period of time that began at the occurrence of another specified event. (2) A time interval allotted for certain operations to occur; for example, response to polling or addressing before system operation is interrupted and must be restarted.

token

**token**. A sequence of bits passed from one device to another along the token ring. When the token has data appended to it, it becomes a frame.

token ring

**token** **ring**. A network with a ring topology that passes tokens from one attaching device to another. For example, the IBM Token-Ring Network.

token-ring interface coupler (TIC)

**token-ring** **interface** **coupler** **(TIC)**. An adapter that can connect a 3720, 3725, or 3745 Communication Controller to an IBM Token-Ring Network.

transmission control (TC) layer

**transmission** **control** **(TC)** **layer**. In SNA, the layer within a half-session that synchronizes and paces session-level data traffic, checks session sequence numbers of requests, and enciphers and deciphers end-user data. Transmission control has two components: the connection point manager and session control. See also *half-session*.

transmission control unit (TCU)

**transmission** **control** **unit** **(TCU)**. A communication control unit whose operations are controlled solely by programmed instructions from the computing system to which the unit is attached; no program is stored or executed in the unit. Examples are the IBM 2702 and 2703 Transmission Controls. Contrast with *communication* *controller*.

transmission line

**transmission** **line**. Synonym for *telecommunication* *line*.

tutorial

**tutorial**. Online information presented in a teaching format.

type 2.1 node (T2.1 node)

**type** **2.1** **node** **(T2.1** **node)**. A node that can attach to an SNA network as a peripheral node using the same protocols as type 2.0 nodes. Type 2.1 nodes can be directly attached to one another using peer-to-peer protocols. See *end* *node*, *node*, and *subarea* *node*. See also *node* *type*.

type 2.1 node (T2.1 node) control point domain

**type** **2.1** **node** **(T2.1** **node)** **control** **point** **domain**. The CP, its logical units (LUs), links, link stations, and all resources that it activates and deactivates..

<a id="GLS"></a>

U

uninterpreted name

**uninterpreted** **name**. In SNA, a character string that a system services control point (SSCP) is able to convert into the network name of a logical unit (LU). Typically, an uninterpreted name is used in a logon or Initiate request from a secondary logical unit (SLU) to identify the primary logical unit (PLU) with which the session is requested.

user

**user**. Anyone who requires the services of a computing system.

user-application network

**user-application** **network**. A configuration of data processing products, such as processors, controllers, and terminals, established and operated by users for the purpose of data processing or information exchange, which may use services offered by communication common carriers or telecommunication Administrations. Contrast with *public* *network*..

<a id="GLS"></a>

V

vector

**vector**. The MAC frame information field.

virtual circuit

**virtual** **circuit**. (TC97) In packet switching, those facilities provided by a network that give the appearance to the user of an actual connection. Contrast with *physical* *circuit*. See also *data* *circuit*.

Virtual Machine (VM)

Virtual Machine (VM). A licensed program whose full name is the Virtual Machine/System Product (VM/SP). It is a software operating system that manages the resources of a real processor to provide virtual machines to end users. As a time-sharing system control program, it consists of the virtual machine control program (CP), the conversational monitor system (CMS), the group control system (GCS), and the interactive problem control

system? (IPCS).

Virtual Storage Extended (VSE)

**Virtual** **Storage** **Extended** **(VSE)**. An IBM licensed program whose full name is the Virtual Storage Extended/Advanced Function. It is a software operating system controlling the execution of programs.

Virtual Telecommunications Access Method (VTAM)

**Virtual** **Telecommunications** **Access** **Method** **(VTAM)**. An IBM licensed program that controls communication and the flow of data in an SNA network. It provides single-domain, multiple-domain, and interconnected network capability.

VM

**VM**. Virtual Machine. Its full name is Virtual Machine/System Product. Synonymous with *VM/SP*.

VM/SP

**VM/SP**. Virtual Machine/System Product. Synonym for *VM*.

VSE

**VSE**. Virtual Storage Extended. Synonymous with *VSE/Advanced* *Functions*.

VSE/Advanced Functions

**VSE/Advanced** **Functions**. The basic operating system support needed for a VSE-controlled installation. Synonym for *VSE*.

VTAM

**VTAM**. Virtual Telecommunications Access Method (IBM licensed program). Its full name is Advanced Communications Function for the Virtual Telecommunications Access Method. Synonymous with *ACF/VTAM*.

VTAM operator command

**VTAM** **operator** **command**. A command used to monitor or control a VTAM domain. See also *definition* *statement*..

<a id="GLS"></a>

W

wide area network

**wide** **area** **network**. A network that provides data communication capability in geographic areas larger than those serviced by local area networks. Wide area networks may extend across public rights-of-way. Contrast with *local* *area* *network*.

window

**window**. (1) In SNA, synonym for *pacing* *group*. (2) On a visual display terminal, a small amount of information in a framed-in area on a panel that overlays part of the panel. (3) In data communication, the number of data packets a data terminal equipment (DTE) or data circuit-terminating equipment (DCE) can send across a logical channel before waiting for authorization to send another data packet. The window is the main mechanism of pacing, or flow control, of packets.

wire fault

**wire** **fault**. An error condition caused by a break in the wires or a short between the wires (or shield) in a segment of cable.

wrap

**wrap**. In general, to go from the maximum to the minimum in computer storage. For example, the continuation of an operation from the maximum value in storage to the first minimal value..

<a id="GLS"></a>

X

X.21

**X.21**. See *Recommendation* *X.21*.

X.25

**X.25**. See *Recommendation* *X.25*.

X.25 NCP Packet Switching Interface (NPSI)

**X.25** **NCP** **Packet** **Switching** **Interface** **(NPSI)**. The X.25 Network Control Program Packet Switching Interface, which is an IBM licensed program that allows SNA users to communicate over packet-switched data networks that have interfaces complying with Recommendation X.25 (Geneva 1980) of the International Telegraph and Telephone Consultative Committee (CCITT). It allows SNA programs to communicate with SNA equipment or with non-SNA equipment over such networks. In addition, this product may be used to attach native X.25 equipment to SNA host systems without a packet network. See also *Recommendation* *X.25* *(Geneva* *1980)*.

XA

**XA**. Extended architecture.

XID

**XID**. Exchange identification..

<a id="GLS"></a>

---

[Previous](3-30.md) | [Index](README.md) | [Next](bibliography.md)
