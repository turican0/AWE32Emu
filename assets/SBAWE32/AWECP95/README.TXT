ReadMe File For AWE Control Panel (Windows 95 version)
======================================================

This is the ReadMe file for the Sound Blaster AWE Control Panel
(for Windows 95). This ReadMe file contains the following sections:

        1. Introduction
        2. Hardware and Software requirements
        3. Contents of the Software Package        
        4. Installing the AWE Control Panel
        5. Additional Information
             (i) What is SoundFont 2.0?
             (ii) Creative FTP site & World Wide Web site on the Internet
               

1.  Introduction
    ============

    This is the release of the AWE Control Panel (version 2.05)
    for Windows 95. 

    This version of the AWE Control Panel includes the following
    new features:

        * Support for SoundFont 2.0 sound-font bank format
        * Support for MPU-401 emulation (to enable/disable)


2.  Hardware and Software Requirements
    ==================================

    To make use of this software package, you will require the
    following:

    Hardware - A Sound Blaster 32 or Sound Blaster AWE32 audio card
               in your PC system. Your audio card must already be
               configured and fully functional in Windows 95.

    Software - Microsoft Windows 95 on your PC
             - The Audio software included with your audio card.
	     - The latest Windows 95 audio drivers for the Sound Blaster
	       32 or Sound Blaster AWE32 - SBAWE32.DRV and SBAWE.VXD
	       (versions 4.25 and above) 

   *** IMPORTANT ***

    * Ensure that you have already installed the Audio software
      included with your audio card - including the (older version
      of the) AWE Control Panel for Windows 95.

      If you have not previously installed the Audio software for
      your audio card, please do so now.

    * If you do NOT have the latest Windows 95 audio drivers, please
      download the latest Sound Blaster audio drivers update pack for
      Windows 95 from the Creative Labs FTP site or World Wide Web site.


3.  Contents of the Software Package
    ================================

    This software package contains the following files:

    AWECP32.EXE     AWE Control Panel application for Windows 95.
    AWE32RES.DLL    AWE Control Panel resources dll.
    AWECP.HLP       AWE Control Panel help file.
    AWECP.CNT       Contents page for the AWE Control Panel help file.
    README.TXT      This file.
    

4.  Installing the AWE Control Panel
    ================================

    To install the AWE Control Panel, just follow these steps:

    A. Ensure that you have already installed the Audio software
       included with your audio card - including the (older version
       of the) AWE Control Panel for Windows 95.

       If you have not previously installed the Audio software for
       your audio card, please do so now.
       
    B. Make a back-up of your existing AWE Control Panel files. You
       can do this by using the DOS Copy command. For example - at the
       DOS prompt, type:

       COPY AWECP32.EXE AWECP32.BAK

       This will create a back-up copy of your existing AWECP32.EXE
       file and rename it as AWECP32.BAK

       You should make a back-up of all the files that you will be
       updating (namely AWECP32.EXE, AWE32RES.DLL, AWECP.HLP, AWECP.CNT).
       You will find these 4 files in the target directory that you
       installed your Audio software to, under the subdirectories
       \PROGRAM (for AWECP32.EXE & AWE32RES.DLL) and \HELP (for AWECP.HLP
       and AWECP.CNT).

       For example, if you installed your Audio software to the (default)
       directory \PROGRAM FILES\CREATIVE\CTSND, you need to go to
       \PROGRAM FILES\CREATIVE\CTSND\PROGRAM to find AWE32CP.EXE and
       AWE32RES.DLL). Similarly for the 2 Help files, look in the
       \PROGRAM FILES\CREATIVE\CTSND\HELP subdirectory.

    C. Copy the new AWE Control Panel files to the following
       subdirectories (assuming that your original Audio software
       was installed to the default \PROGRAM FILES\CREATIVE\CTSND
       directory)

       * Copy AWECP32.EXE and AWE32RES.DLL to

       \PROGRAM FILES\CREATIVE\CTSND\PROGRAM  

       * Copy AWECP.HLP and AWECP.CNT to

       \PROGRAM FILES\CREATIVE\CTSND\HELP 

       As you have already made a back-up of your older version of these
       files, you can simply overwrite them with the newer versions
       included in this software package.

    D. To launch the AWE Control Panel, simply run AWECP32.EXE

       Having previously installed the Audio software package on your
       system, a shortcut to the AWE Control Panel should already have
       been created by the Audio software installation program. You
       can find this shortcut in the Program Group folder containing
       your Audio software icons (look for the icon representing the
       AWE Control Panel - it should be described as 'AWE Control Panel')

       Alternately, you can create a shortcut to the AWECP32.EXE file
       from your Windows 95 desktop. Please refer to your Windows 95
       documentation on how to create shortcuts.


5. Additional Information
   ======================

(i)  What is SoundFont 2.0?

     SoundFont 2.0 is a new digital audio file format for wavetable
     synthesizer sounds.  It is a public standard that is portable,
     extensible, and carefully designed to support arbitrarily complex
     modulation and synthesis networks.  

(ii) Creative FTP site & World Wide Web site on the Internet

     For more information on SoundFont 2.0, please visit the Creative 
     Labs World Wide Web site at

                http://www.creaf.com/emu/

     For the latest drivers for your Creative audio card, please visit the
     Creative Labs FTP site at

		ftp.creaf.com

                         *** End of ReadMe File ***
