/****************************************************************
 *
 * $Source: /pv/CvsTree/pv/gen/src/prg/methods/RARE/parsRelations.c,v $
 *
 * Copyright (c) 2002
 * Bruker BioSpin MRI GmbH
 * D-76275 Ettlingen, Germany
 *
 * All Rights Reserved
 *
 * $Id: parsRelations.c,v 1.33.2.11 2009/08/05 09:03:51 dgem Exp $
 *
 ****************************************************************/

static const char resid[] = "$Id: parsRelations.c,v 1.33.2.11 2009/08/05 09:03:51 dgem Exp $(C) 2002 Bruker BioSpin MRI GmbH";


#define DEBUG		0
#define DB_MODULE	0
#define DB_LINE_NR	0

#include "method.h"

/* ------------------------------------------------------------
 * backbone
 * The main part of method code. The consitency of all parameters
 * is checked here, relations between them are resolved and,
 * finally, functions setting the base level parameters are
 * called.
 */


void backbone( void )
{
    YesNo refAttIsAvailable=No;
    int dim;

    double referenceAttenuation=0,
           minFov[3] = {1e-3, 1e-3, 1e-3},
                       minThickness,fixedTime;
    int    nSlices;
    YesNo RetVal;


    DB_MSG(("-->backbone\n"));

    PVM_UserType  = Expert_User;
    STB_DefaultUserTypeHandler(); /* this controls param visibility
				   for expert/routine */

    STB_UpdateNuclei(Yes);

    /* allow higher gradient amplitudes for non oblique slices */

    ConstrainGradientLimits(PVM_MajSliceOri);

    /* coexistance of modules: */
    if(PVM_SelIrOnOff == On)
    {
        PVM_BlBloodOnOff = Off;
    }

    /* handle RF pulses */

    if(PVM_DeriveGains == Yes)
        refAttIsAvailable =
            STB_GetRefAtt(1,PVM_Nucleus1,&referenceAttenuation);
    else
        refAttIsAvailable = No;

    STB_UpdateRFPulse("ExcPulse",
                      &ExcPulse,
                      refAttIsAvailable,
                      referenceAttenuation);
    STB_UpdateExcPulseEnum("ExcPulseEnum",
                           &ExcPulseEnum,
                           ExcPulse.Filename,
                           ExcPulse.Classification);

    STB_UpdateRFPulse("RefPulse",
                      &RefPulse,
                      refAttIsAvailable,
                      referenceAttenuation);

    STB_UpdateRfcPulseEnum("RefPulseEnum",
                           &RefPulseEnum,
                           RefPulse.Filename,
                           RefPulse.Classification);

    PVM_ExcPulseAngle = RfcFlipAngle = RefPulse.FlipAngle;

    /* sequence atoms: */

    PVM_3dPhaseGradientTime = PVM_2dPhaseGradientTime;
    PVM_ExSliceRephaseTime  = PVM_ReadDephaseTime;
    PVM_EchoPosition = 50.0;
    /* readout is on during phase encoding, thus:  */

    PVM_AcqStartWaitTime =
        PVM_2dPhaseGradientTime - PVM_RampTime + PVM_GradDelayTime;

    /* spoilers */
    SliceSpoilerStrength = MIN_OF(SliceSpoilerStrength, 100.0);
    SliceSpoilerStrength = MAX_OF(SliceSpoilerStrength, 0.0);
    SliceSpoilerDuration = MAX_OF(SliceSpoilerDuration, 2*PVM_RiseTime);
    SliceSpoilerDuration = MIN_OF(SliceSpoilerDuration, 10.0);

    RepetitionSpoilerStrength = MIN_OF(RepetitionSpoilerStrength, 100.0);
    RepetitionSpoilerStrength = MAX_OF(RepetitionSpoilerStrength, 0.0);
    RepetitionSpoilerDuration = MAX_OF(RepetitionSpoilerDuration, 2*PVM_RiseTime);
    RepetitionSpoilerDuration = MIN_OF(RepetitionSpoilerDuration, 10.0);

    /* excitation pulse */
    PVM_ExSlicePulseLength   = ExcPulse.Length;
    PVM_ExSliceBandWidth     = ExcPulse.Bandwidth *
                               PVM_SliceBandWidthScale/100.0;
    PVM_ExSliceRephaseFactor = ExcPulse.RephaseFactor*
                               ExcPulse.TrimRephase / 100.0;

    /* begin Update Geometry: */

    /*
     * 1: in-plane geometry:
     * The STB_StandardInplaneGeoParHandler is called twice:
     * first, with a dummy value of minFov, to make size constraints;
     * then, after the true minFov is found, to do the rest.
     * (because the sizes must be set before we find minFov)
     */



    STB_StandardInplaneGeoParHandler(minFov,2.0);

    /* once the image size is decided (after first update of inpl geo), we
       can update the Encoding parameters, to get the acquisition size (PVM_EncMatrix)*/

    STB_UpdateEncoding(PTB_GetSpatDim(),  /* total dimensions */
                       PVM_Matrix,        /* image size */
                       PVM_AntiAlias,     /* a-alias */
                       &PVM_RareFactor,   /* segment size */
                       SEG_INTERLEAVED,   /* segmenting mode */
                       Yes,               /* ppi in 2nd dim allowed */
                       Yes,               /* ppi ref lines in 2nd dim allowed */
                       Yes);               /* partial ft in 2nd dim allowed */

    /* update bandwidth and acquisition time */

    STB_UpdateDigPars(&PVM_EffSWh,
                      PVM_EncMatrix[0],
                      PVM_AntiAlias,
                      &PVM_AcquisitionTime);

    LocalGeometryMinimaRels(minFov, &minThickness);

    dim = PTB_GetSpatDim();

    /*
     * Constrain minimum fov in 3D phase direction and
     * minimum slice thickness in case of 3D measurements
     */

    if(dim == 3)
    {
        double min;

        min=MAX_OF(minFov[2],minThickness);
        minFov[2] = minThickness = min;

    }


    STB_StandardInplaneGeoParHandler(minFov,2.0);



    /* 2: slice geometry
     *   The maximun slice number per packages is set to 1 for 3D
     *   and free for 2D . The total maximum number of slices is
     *   defined by the return value of CFG_MaxSlices() called by
     *   STB_UpdateSliceGeoPars if no constrain is given by the
     *   arguments (value 0)
     */

    dim = PTB_GetSpatDim();

    if(dim == 3)
    {
        /*
         * Connect slice thickness to FOV in 3rd direction.
         */
        PVM_SliceThick = PVM_Fov[2];

        /*
         *  constrain maximum slices per package to 1
         */

        STB_UpdateSliceGeoPars(0,0,1,minThickness);
    }
    else
    {
        /*
         *  no constrain to slices in 2D mode
         */
        STB_UpdateSliceGeoPars(0,0,0,minThickness);

    }

    if(PVM_BlBloodOnOff == On)
        STB_UpdateSliceGeoPars(0,1,0,minThickness);

    /* end Update Geometry */


    LocalGradientStrengthRels();
    LocalFrequencyOffsetRels();

    PVM_NRepetitions = MAX_OF(1,PVM_NRepetitions);

    Local_NAveragesRange();

    PVM_NEchoImages = MAX_OF(1, PVM_NEchoImages);
    PVM_NEchoImages = MIN_OF(8, PVM_NEchoImages);

    NEchoes = PVM_RareFactor * PVM_NEchoImages;

    /* handling of modules */

    STB_UpdateFatSupModule(PVM_Nucleus1);
    STB_UpdateMagTransModule();

    /* Jiadi Xu add for MT_Series*/
    /* Jiadi Xu add for MT_Series*/
    ParxRelsShowInEditor("Number_Offset_Experiments,MTOffsetMode,MT_Offsets");
    ParxRelsShowInEditor("Initial_MT_Offset,Final_MT_Offset,MT_Offsets_NoM0,Offset_Step_Hz");
    ParxRelsShowInEditor("M0Option,Number_Offsets");
    ParxRelsMakeNonEditable("MT_Offsets,MT_Offsets_NoM0,Number_Offset_Experiments");
    ParxRelsMakeEditable("M0Option");
    ParxRelsHideInEditor("MTListFile");

    if (MTOffsetMode== Enter_Manually)
    {
        ParxRelsHideInEditor("Initial_MT_Offset,Final_MT_Offset,Offset_Step_Hz");
        ParxRelsMakeEditable("MT_Offsets_NoM0");

    }

    if (MTOffsetMode== From_File)
    {
        ParxRelsHideInEditor("Initial_MT_Offset,Final_MT_Offset,Offset_Step_Hz");
        ParxRelsShowInEditor("MTListFile");
    }

    if  (PVM_MagTransOnOff == Off)
    {
        ParxRelsHideInEditor("Number_Offset_Experiments,MTOffsetMode,MT_Offsets,MT_Offsets_NoM0");
        ParxRelsHideInEditor("Initial_MT_Offset,Final_MT_Offset,Offset_Step_Hz,M0Option,Number_Offsets");
        ParxRelsHideInEditor("MTListFile");
    }

    ParxRelsMakeNonEditable("Offset_Step_Hz");

    PVM_MagTransOffset=0;
    ParxRelsHideInEditor("PVM_MagTransOffset");
    PVM_MagTransScheme = Free_Pulse;
    ParxRelsMakeNonEditable("PVM_MagTransScheme");
    /*************************************/

    STB_UpdateSatSlicesModule(PVM_Nucleus1);
    STB_UpdateFlowSaturationModule(PVM_Nucleus1);
    STB_UpdateTriggerModule();
    STB_UpdateEvolutionModule(&EvolutionDuration);




    fixedTime = RepetitionSpoilerDuration +CFG_AmplifierEnable() + PVM_ExSlicePulseLength/2.0;
    /* Update for Black Blood Module   */
    {
        double slabthick[1];
        slabthick[0] = (PVM_SPackArrSliceDistance[0] * (PVM_SPackArrNSlices[0]-1)) + PVM_SliceThick;
        STB_UpdateBlBloodModule(PVM_Nucleus1,slabthick,PVM_SPackArrSliceOffset,1,fixedTime);
    }

    echoTimeRels();
    SliceSegDurRels();
    nSlices = GTB_NumberOfSlices( PVM_NSPacks, PVM_SPackArrNSlices);
    RetVal = STB_UpdateSelIrModule(PVM_Nucleus1,PVM_SliceThick,PVM_SliceOffset,nSlices,&SliceSegDur,0,fixedTime);
    if(PVM_SelIrOnOff==On)
        ParxRelsCopyPar("PVM_InversionTime","PVM_SelIrInvTime");
    if(PVM_BlBloodOnOff==On)
        ParxRelsCopyPar("PVM_InversionTime","PVM_BlBloodInvTime");

    if(PVM_SelIrOnOff==Off&&PVM_BlBloodOnOff==Off)
    {
        PVM_InversionTime = 0.0;
        ParxRelsHideInEditor("PVM_InversionTime");
    }

    repetitionTimeRels();

    /* set baselevel acquisition parameter */

    SetBaseLevelParam();


    /* set baselevel reconstruction parameter */
    SetRecoParam();

    DB_MSG(("<--backBone\n"));
}




/*==============================================================
 * RfcPulseAngleRelation
 * Redirected relation of PVM_ExcPulseAngle
 * PVM_ExcPulseAngle, the angle editable in the scan editor
 * is used for the refocusing pulse angle in this method.
 * =============================================================*/

void RfcPulseAngleRelation(void)
{
    DB_MSG(("-->RfcPulseAngleRelation\n"));

    RefPulse.FlipAngle = PVM_ExcPulseAngle;
    RefPulseRange();
    backbone();

    DB_MSG(("<--RfcPulseAngleRelation\n"));
}

/*==============================================================
 * Redirected relation of RfcFlipAngle
 * RfcFlipAngle: the angle editable in the method class
 * =============================================================*/

void RfcFlipAngleRel(void)
{
    DB_MSG(("-->RfcFlipAngleRelation\n"));

    PVM_ExcPulseAngle = RfcFlipAngle;
    RfcPulseAngleRelation();

    DB_MSG(("<--RfcFlipAngleRelation\n"));
}

/*===========================================================
 *
 *  examples for relations concearning special pulses and
 *  pulselists
 *
 *==========================================================*/



/* ====================================================================
 *  ExcPulseEnumRelation
 * Relation of ExcPulseEnum (a dynamic enmueration parameter which
 * allows to select one of the existing library exc. pulses)
 * Sets the name and the clasification  of the pulse perameter ExcPulse
 * according to the selected enum value.
 * ====================================================================*/
void ExcPulseEnumRelation(void)
{
    YesNo status;

    DB_MSG(("-->ExcPulsesEnumRelation\n"));

    /* set the name and clasification of ExcPulse: */
    status = STB_UpdateExcPulseName("ExcPulseEnum",
                                    &ExcPulseEnum,
                                    ExcPulse.Filename,
                                    &ExcPulse.Classification);

    /* exc pulse changed - check it */
    ExcPulseRange();

    /* call the method relations */
    backbone();

    DB_MSG(("<--ExcPulseEnumRelation\n"));
}

/* ===================================================================
 * Relation of ExcPulse
 *
 * All pulses of type PVM_RF_PULSE_TYPE must have relations like this.
 * However, if you clone this funtion for a different pulse parameter
 * remember to replace the param name in the call to UT_SetRequest!
 *
 * IMPORTANT: this function should not be invoked in the backbone!
 * ===================================================================*/
void ExcPulseRelation(void)
{
    DB_MSG(("-->ExcPulseRelation\n"));

    /*
     * Tell the request handling system that the parameter
     * ExcPulse has been edited
     */

    UT_SetRequest("ExcPulse");

    /* Check the values of ExcPulse */
    ExcPulseRange();

    /* call the backbone; further handling will take place there
       (by means of STB_UpdateRFPulse)  */

    backbone();

    DB_MSG(("<--ExcPulseRelation\n"));
}



/* --------------------------------------------------------------
   Relation of RefPulseEnum
   --------------------------------------------------------------*/
void RefPulseEnumRelation(void)
{
    YesNo status;
    DB_MSG(("-->RefPulsesEnumRelation\n"));

    /* set the name and clasification of RefPulse: */

    status = STB_UpdateRfcPulseName("RefPulseEnum",
                                    &RefPulseEnum,
                                    RefPulse.Filename,
                                    &RefPulse.Classification);

    /* ref pulse changed - check it */
    RefPulseRange();

    /* call the method relations */
    backbone();

    DB_MSG(("<--RefPulseEnumRelation\n"));
}


/* -----------------------------------------------------------
   Relation of RefPulse
   -----------------------------------------------------------*/
void RefPulseRelation(void)
{
    DB_MSG(("-->RefPulseRelation\n"));

    /*
     *  Tell the request handling system that the parameter
     *  RefPulse has been edited
     */

    UT_SetRequest("RefPulse");

    /* Check the values of RefPulse */
    RefPulseRange();

    /*
     * call the backbone; further handling will take place
     * there (by means of STB_UpdateRFPulse)
     */

    backbone();

    DB_MSG(("<--RefPulseRelation\n"));
}


/*============================================================
 * redirected relation of PVM_EchoTime1 (eff. TE in scan editor)
 * Calculates echo spacing and calls backbone.
 *=============================================================*/

void localEchoRels(void)
{
    double denom;

    DB_MSG(("-->localEchoRels\n"));

    if(ParxRelsParHasValue("PVM_EncCentralStep1") && PVM_EncCentralStep1>=1)
        denom = PVM_EncCentralStep1;
    else
        denom = 1.0;

    if(denom)
        PVM_EchoTime = PVM_EchoTime1/denom;
    backbone();

    DB_MSG(("<--localEchoRels\n"));
}

/* =============================================================
 * relations of EffectiveTE (local parameter)
 * ============================================================ */
void effTErels(void)
{
    DB_MSG(("-->effTErels\n"));

    PVM_EchoTime1 = EffectiveTE[0];
    localEchoRels();

    DB_MSG(("<--effTErels\n"));
}

/*==============================================================
 * relation of NDummyScans
 *==============================================================*/


void dsRelations(void)
{

    DB_MSG(("-->dsRelations\n"));

    dsRange();
    backbone();

    DB_MSG(("<--dsRelations\n"));

}

void dsRange(void)
{
    if(ParxRelsParHasValue("NDummyScans") == No)
        NDummyScans = 0;
    else
        NDummyScans = MAX_OF(0, NDummyScans);
}


/****************************************************************/
/*	         L O C A L   F U N C T I O N S			*/
/****************************************************************/



void ExcPulseRange(void)
{
    DB_MSG(("-->ExcPulseRange\n"));

    /* allowed clasification */

    switch(ExcPulse.Classification)
    {
    default:
        ExcPulse.Classification = LIB_EXCITATION;
        break;
    case LIB_EXCITATION:
    case PVM_EXCITATION:
    case USER_PULSE:
        break;
    }

    /* allowed angle for this pulse */

    ExcPulse.FlipAngle = MIN_OF(90.0,ExcPulse.FlipAngle);


    /* general verifiation of all pulse atributes  */

    STB_CheckRFPulse(&ExcPulse);

    DB_MSG(("<--ExcPulseRange\n"));

}

void RefPulseRange(void)
{
    DB_MSG(("-->RefPulseRange\n"));

    /* allowed clasification */

    switch(RefPulse.Classification)
    {
    default:
        RefPulse.Classification = LIB_REFOCUS;
        break;
    case LIB_REFOCUS:
    case PVM_REFOCUS:
    case USER_PULSE:
        break;
    }

    /* allowed angle for this pulse */

    RefPulse.FlipAngle = MIN_OF(270,RefPulse.FlipAngle);


    /* general verifiation of all pulse atributes  */

    STB_CheckRFPulse(&RefPulse);

    DB_MSG(("<--RefPulseRange\n"));

}

void echoTimeRels( void )
{
    int i;
    double fill;

    DB_MSG(("-->echoTimeRels\n"));

    /*
     * echo spacing
     */

    /* TE limit due to 90-180 events */
    minTE1 =
        (PVM_ExSlicePulseLength * PVM_ExSliceRephaseFactor/100.0 +
         PVM_RampTime                                            +
         PVM_InterGradientWaitTime                               +
         PVM_GradDelayTime                                       +
         PVM_ExSliceRephaseTime                                  +
         PVM_InterGradientWaitTime                               +
         SliceSpoilerDuration                                    +
         PVM_GradDelayTime                                       +
         RefPulse.Length/2.0 ) *2.0;

    /* TE limit due to 180-180 events */

    fill = PVM_GradDelayTime + PVM_2dPhaseGradientTime - 0.04 - PVM_DigEndDelOpt;
    fill = fill < 0.0 ? 2*fabs(fill) : 0.0;



    minTE2 =
        2*SliceSpoilerDuration       +
        4*PVM_GradDelayTime          +
        RefPulse.Length              +
        2*PVM_2dPhaseGradientTime    +
        2*PVM_InterGradientWaitTime  +
        2*0.04                       +
        PVM_AcquisitionTime;


    PVM_MinEchoTime = MAX_OF(minTE1,minTE2+fill);

    PVM_EchoTime = MAX_OF(PVM_MinEchoTime,PVM_EchoTime);

    /*
     * effective echo times
     */

    PARX_change_dims("EffectiveTE", PVM_NEchoImages);
    EffectiveTE[0] =  PVM_EchoTime*PVM_EncCentralStep1;
    for(i=1; i<PVM_NEchoImages; i++)
        EffectiveTE[i] =  EffectiveTE[0] + i * PVM_RareFactor * PVM_EchoTime;

    /* Echo Parameters for Scan Editor  */

    PVM_EchoTime1 = EffectiveTE[0];
    PVM_EchoTime2 = PVM_NEchoImages>1? EffectiveTE[1]:PVM_EchoTime1;

    DB_MSG(("<--echoTimeRels\n"));
}

void SliceSegDurRels( void)
{
    double tail,amplifierenable;

    amplifierenable = CFG_AmplifierEnable();
    if(PVM_FlipBackOnOff == On)
        tail = PVM_EchoTime + PVM_ExSlicePulseLength / 2.0 + PVM_ExSliceRampDownTime;
    else
        tail = PVM_AcquisitionTime *(100.0 - PVM_EchoPosition) / 100.0 +
               PVM_GradDelayTime + /* d3 */
               PVM_2dPhaseGradientTime    +
               (PVM_EchoTime - minTE2)/2.0 +0.04 + PVM_InterGradientWaitTime; /* d2 */

    MinSliceSegDur =   RepetitionSpoilerDuration        +
                       PVM_GradDelayTime                + /*d3*/
                       amplifierenable                  +
                       PVM_ExSlicePulseLength / 2.0     +
                       PVM_EchoTime*NEchoes             +
                       tail                             +
                       PVM_InterGradientWaitTime; /* min d0 */
    /*SliceSegDur = MAX_OF(SliceSegDur,MinSliceSegDur);*/
    SliceSegDur = MinSliceSegDur;
}

void repetitionTimeRels( void )
{
    int nSlices,dim;
    double TotalTime, tail,amplifierenable;

    DB_MSG(("-->repetitionTimeRels"));

    TotalTime = 0.0;
    amplifierenable = CFG_AmplifierEnable();
    nSlices = GTB_NumberOfSlices( PVM_NSPacks, PVM_SPackArrNSlices );
    if( PVM_ErrorDetected == Yes )
    {
        UT_ReportError("backbone: In function call!");
        return;
    }


    /* tail:
     * time from last echo to the end of sequence;
     * Will be used below.
     */

    if(PVM_FlipBackOnOff == On)
        tail = PVM_EchoTime + PVM_ExSlicePulseLength / 2.0 + PVM_ExSliceRampDownTime;
    else
        tail = PVM_AcquisitionTime *(100.0 - PVM_EchoPosition) / 100.0 +
               PVM_GradDelayTime + /* d3 */
               PVM_2dPhaseGradientTime    +
               (PVM_EchoTime - minTE2)/2.0 +0.04 + PVM_InterGradientWaitTime; /* d2 */

    if(PVM_SelIrOnOff)
    {
        PVM_MinRepetitionTime =  SliceSegDur * nSlices + PVM_SelIrModuleTime + 0.01 /* min d10 */;
        /*SliceSegDelay = SliceSegDur - MinSliceSegDur; */
        SliceSegDelay = 0.0;
    }
    else
    {
        PVM_MinRepetitionTime =
            nSlices * (
                PVM_FatSupModuleTime +
                PVM_MagTransModuleTime +
                PVM_FovSatModuleTime +
                PVM_InFlowSatModuleTime +
                PVM_GradDelayTime +   /* d3 */
                0.01 +
                RepetitionSpoilerDuration +
                amplifierenable +
                PVM_ExSlicePulseLength / 2.0 +
                PVM_EchoTime*NEchoes +
                tail +
                PVM_InterGradientWaitTime /* min d0 */
            );

        PVM_MinRepetitionTime += PVM_BlBloodModuleTime;
    }
    PVM_RepetitionTime = ( PVM_RepetitionTime < PVM_MinRepetitionTime ?
                           PVM_MinRepetitionTime : PVM_RepetitionTime );

    if(PVM_SelIrOnOff)
    {
        if(PVM_RepetitionTime > PVM_MinRepetitionTime)
            SliceSegEndDelay = PVM_RepetitionTime - PVM_MinRepetitionTime + 0.01 - SliceSegDelay;
        else
            SliceSegEndDelay = 0.01;
    }
    /* Calculate Total Scan Time and Set for Scan Editor */

    dim = PTB_GetSpatDim();
    if( dim >1 )
        TotalTime = PVM_RepetitionTime *PVM_EncMatrix[1]*PVM_NAverages;
    if( dim >2 )
        TotalTime = TotalTime *PVM_EncMatrix[2];

    TotalTime /= PVM_RareFactor;

    /* time for one repetition */
    OneRepTime = TotalTime/1000.0;

    if(PVM_EvolutionOnOff && (PVM_NEvolutionCycles > 1))
        TotalTime = TotalTime * PVM_NEvolutionCycles+ EvolutionDuration;


    TotalTime = TotalTime * PVM_NRepetitions;

    UT_ScanTimeStr(PVM_ScanTimeStr,TotalTime);
    ParxRelsShowInEditor("PVM_ScanTimeStr");
    ParxRelsMakeNonEditable("PVM_ScanTimeStr");

    DB_MSG(("<--repetitionTimeRels\n"));

}



void LocalGeometryMinimaRels(double *min_fov, double *min_thickness )
{
    /*
     * This function calculates the minima for the fields-of-view in all
     * three directions as well as the minimum slice thickness. It is always
     * assumed that all slices have the same thickness
     *
     * The results is returned in min_fov[0..2] and min_thickness.
     */
    double readRampInteg;  /* normalised integral rising read gradient ramp   */
    double readDephInteg;  /* normalised integral read dephase gradient       */

    double sliceRampInteg; /* normalised integral falling slice gradient ramp */
    double sliceRephInteg; /* normalised integral slice rephase gradient      */

    DB_MSG(("-->LocalGeometryMinimaRels\n"));

    switch( PTB_GetSpatDim() )
    {
    case 3:
        /* PHASE ENCODING GRADIENT - 3nd DIM
        *
        * Step #1:
        * Calculate the normalised integral of the phase encoding gradient
        * in the 3rd dimension
        *
        * The variable Phase3dInteg is a parameter defined in the file:
        * parsDefinition.h but it is NOT included in the definition of
        * MethodClass
        * that appears in parsLayout.h, The value of Phase3dInteg determined
        * here is used later in "LocalGradientStrengthRels()"
        */
        Phase3dInteg = MRT_NormGradPulseTime ( PVM_3dPhaseGradientTime,
                                               PVM_3dPhaseRampUpTime,
                                               PVM_3dPhaseRampUpIntegral,
                                               PVM_3dPhaseRampDownTime,
                                               PVM_3dPhaseRampDownIntegral );
        /*
        * Step #2:
        * Calculate the resulting minimum field-of-view in that direction
        */

        min_fov[2] = MRT_PhaseFov( Phase3dInteg,
                                   PVM_Matrix[2],
                                   PVM_Lim3dPhaseGradient,
                                   PVM_GradCalConst );
    /* falls through */
    case 2:
        /* PHASE ENCODING GRADIENT - 2nd DIM
        *
        * the same for the second dimension
        *
        * The variable Phase2dInteg is a parameter defined in the file:
        * parsDefinition.h but it is NOT included in the definition of
        * MethodClass that appears in parsLayout.h, The value of
        * Phase2dInteg determined here is used later in
        * "LocalGradientStrengthRels()"
        */

        Phase2dInteg = MRT_NormGradPulseTime ( PVM_2dPhaseGradientTime,
                                               PVM_2dPhaseRampUpTime,
                                               PVM_2dPhaseRampUpIntegral,
                                               PVM_2dPhaseRampDownTime,
                                               PVM_2dPhaseRampDownIntegral );

        min_fov[1] = MRT_PhaseFov( Phase2dInteg,
                                   PVM_Matrix[1],
                                   PVM_Lim2dPhaseGradient,
                                   PVM_GradCalConst );
    /* falls through */
    case 1:
        /* FREQUENCY ENCODING GRADIENT
        *
        * Step #1:
        * Calculate the normalised integral of the read dephase gradient
        */

        readDephInteg = MRT_NormGradPulseTime( PVM_ReadDephaseTime,
                                               PVM_ReadDephaseRampUpTime,
                                               PVM_ReadDephaseRampUpIntegral,
                                               PVM_ReadDephaseRampDownTime,
                                               PVM_ReadDephaseRampDownIntegral );
        /*
        * Step #2
        * Calculate the normalised integral of the rising ramp of the read
        * gradient preceding data collection
        */


        readRampInteg = MRT_NormGradRampTime( PVM_ReadRampUpTime,
                                              PVM_ReadRampUpIntegral );
        /*
        * Step #3
        * Calculate the ratio of the strength of the read gradient to the
        * strength of the read dephase gradient
        *
        * The variable ReadGradRatio is a parameter defined in the file:
        * parsDefinition.h but it is NOT included in the definition of
        * MethodClass that appears in parsLayout.h, The value of ReadGradRatio
        * determined here is used later in "LocalGradientStrengthRels()"
        */


        ReadGradRatio = MRT_ReadGradRatio( PVM_AcquisitionTime,
                                           PVM_EchoPosition,
                                           PVM_AcqStartWaitTime,
                                           readDephInteg,
                                           readRampInteg );
        /*
        * Step #4
        * Calculate the minimum field of view in the read direction
        */

        min_fov[0] = MRT_MinReadFov( PVM_EffSWh,
                                     ReadGradRatio,
                                     PVM_LimReadGradient,
                                     PVM_LimReadDephaseGradient,
                                     PVM_GradCalConst );
    /* falls through */
    default:
        /* SLICE SELECTION GRADIENT
        *
        * Calculate the normalised integral of the descending gradient
        * ramp after the RF pulse
        */

        sliceRampInteg = MRT_NormGradRampTime( PVM_ExSliceRampDownTime,
                                               PVM_ExSliceRampDownIntegral );
        /*
         * Calculate the normalised integral of the slice selection rephasing
         * gradient
         */
        sliceRephInteg =
            MRT_NormGradPulseTime( PVM_ExSliceRephaseTime,
                                   PVM_ExSliceRephaseRampUpTime,
                                   PVM_ExSliceRephaseRampUpIntegral,
                                   PVM_ExSliceRephaseRampDownTime,
                                   PVM_ExSliceRephaseRampDownIntegral );
        /*
         * Calculate the ratio of the strength of the slice selection
         * gradient to the strength of the slice selection rephase
         * gradient
         *
         * The variable SliceGradRatio is a parameter defined in the file:
         * parsDefinition.h but it is NOT included in the definition of
         * MethodClass that appears in parsLayout.h.
         * The value of SliceGradRatio determined here is used later in
         * "LocalGradientStrengthRels()"
         */
        SliceGradRatio =
            MRT_SliceGradRatio( PVM_ExSlicePulseLength,
                                PVM_ExSliceRephaseFactor,
                                0.0, /*PVM_ExSliceRampDownWaitTime,*/
                                sliceRampInteg,
                                sliceRephInteg );
        /*
        * Calculate the minimum slice thickness
        */

        *min_thickness = MRT_MinSliceThickness( PVM_ExSliceBandWidth,
                                                SliceGradRatio,
                                                PVM_LimExSliceGradient,
                                                PVM_LimExSliceRephaseGradient,
                                                PVM_GradCalConst );
        break;
    }
    DB_MSG(("<--LocalGeometryMinimaRels\n"));
}



void LocalGradientStrengthRels( void )
{
    DB_MSG(("-->LocalGradientStrengthRels\n"));

    /*
     * This function calculates all the gradient strengths
     */

    switch( PTB_GetSpatDim() )
    {
    case 3:
        /*
         * PHASE ENCODING GRADIENT - 3nd DIM
         *
         * Calculate the strength of the 3d phase encoding gradient
         */
        PVM_3dPhaseGradient = MRT_PhaseGrad( Phase3dInteg,
                                             PVM_Matrix[2],
                                             PVM_Fov[2],
                                             PVM_GradCalConst );
    /* falls through */
    case 2:
        /*
         * PHASE ENCODING GRADIENT - 2nd DIM
         *
         * Calculate the strength of the 2d phase encoding gradient
         */
        PVM_2dPhaseGradient = MRT_PhaseGrad( Phase2dInteg,
                                             PVM_Matrix[1],
                                             PVM_Fov[1],
                                             PVM_GradCalConst );
    /* falls through */
    case 1:
        /* FREQUENCY ENCODING GRADIENT */
        PVM_ReadGradient = MRT_ReadGrad( PVM_EffSWh,
                                         PVM_Fov[0],
                                         PVM_GradCalConst );
        PVM_ReadDephaseGradient = MRT_ReadDephaseGrad( ReadGradRatio,
                                  PVM_ReadGradient );
    /* falls through */
    default:
        /* SLICE SELECTION GRADIENT */
        PVM_ExSliceGradient = MRT_SliceGrad( PVM_ExSliceBandWidth,
                                             PVM_SliceThick,
                                             PVM_GradCalConst );
        PVM_ExSliceRephaseGradient = MRT_SliceRephaseGrad( SliceGradRatio,
                                     PVM_ExSliceGradient );
        break;
    }
    DB_MSG(("<--LocalGradientStrengthRels\n"));

}



void LocalFrequencyOffsetRels( void )
{
    int spatDim;
    int i,nslices;

    DB_MSG(("-->LocalFrequencyOffsetRels\n"));

    spatDim = PTB_GetSpatDim();

    nslices = GTB_NumberOfSlices(PVM_NSPacks,PVM_SPackArrNSlices);

    /*
     * Calculate offset in read direction
     */

    MRT_FrequencyOffsetList(nslices,
                            PVM_EffReadOffset,
                            PVM_ReadGradient,
                            PVM_GradCalConst,
                            PVM_ReadOffsetHz );

    /*
     * Calculate slice offset
     */

    MRT_FrequencyOffsetList(nslices,
                            PVM_EffSliceOffset,
                            PVM_ExSliceGradient,
                            PVM_GradCalConst,
                            PVM_SliceOffsetHz );

    if(spatDim == 3)
    {
        for(i=0; i<nslices; i++)
            PVM_EffPhase2Offset[i] = -PVM_EffSliceOffset[i];
    }

    DB_MSG(("<--LocalFrequencyOffsetRels\n"));

}



/*===============================================================
 *
 * Range checking routine for parameter PVM_NAverages
 *
 *==============================================================*/


void Local_NAveragesRange(void)
{
    int ival;
    DB_MSG(("-->Local_NAveragesRange\n"));

    /*
     *  Range check of PVM_NAverages: prevent it to be negative or 0
     */

    if(ParxRelsParHasValue("PVM_NAverages") == No)
    {
        PVM_NAverages = 1;
    }

    ival = PVM_NAverages;
    PVM_NAverages = MAX_OF(ival,1);


    DB_MSG(("<--Local_NAveragesRange\n"));

}



void Local_NAveragesHandler(void)
{

    DB_MSG(("-->Local_NAveragesRange\n"));


    Local_NAveragesRange();

    /*
     *   Averages range check is finished, handle the request by
     *   the method:
     */


    backbone();


    DB_MSG(("<--Local_NAveragesRange\n"));
    return;
}

/* PhaseEncodingStart only used in loadMeth for converting old protocols */
void encodingRelations()
{
    DB_MSG(("-->encodingRelations\n"));

    ParxRelsResetPar("PhaseEncodingStart");

    DB_MSG(("<--encodingRelations\n"));
}



/*===========================================================
 * The update of slice geometry is done in backbone.
 * This function connects the Fov in 3rd spatial dimension
 * to the slice thickness.
 *==========================================================*/

void localHandleSliceGeometry(void)
{
    DB_MSG(("-->localHandleSliceGeometry\n"));

    if(PTB_GetSpatDim()==3)
    {

        STB_IsotropicRange();

        switch(PVM_Isotropic)
        {
        case Isotropic_Fov:
        case Isotropic_All:
            PVM_Fov[0] =
                PVM_Fov[1] =
                    PVM_Fov[2] = PVM_SliceThick;
            break;
        default:
            PVM_Fov[2] = PVM_SliceThick;
            break;
        }
    }

    backbone();


    DB_MSG(("<--localHandleSliceGeometry\n"));
    return;
}


/* rangechecking and redirected relations of PVM_EffSWh */

void EffSWhRange(void)
{
    DB_MSG(("-->EffSWhRange"));

    if(!ParxRelsParHasValue("PVM_EffSWh"))
    {
        PVM_EffSWh = 50000;
    }
    else
    {
        PVM_EffSWh = MIN_OF(MAX_OF(PVM_EffSWh,2000.0),1000000);
    }

    DB_MSG(("-->EffSWhRange"));
    return;
}

void EffSWhRel(void)
{
    DB_MSG(("-->EffSWhRel"));

    EffSWhRange();
    backbone();

    DB_MSG(("-->EffSWhRel"));
    return;
}

void localInversionRel(void)
{
    DB_MSG(("-->localInversionRel"));
    if(!ParxRelsParHasValue("PVM_InversionTime"))
        PVM_InversionTime = 0.0;
    PVM_InversionTime = MAX_OF(PVM_InversionTime,0.0);
    if(PVM_SelIrOnOff==On)
        PVM_SelIrInvTime = PVM_InversionTime;
    if(PVM_BlBloodOnOff==On)
        PVM_BlBloodInvTime = PVM_InversionTime;
    backbone();
    DB_MSG(("-->localInversionRel"));
}


void ConstrainGradientLimits(YesNo not_oblique)
{
    if(not_oblique == Yes)
    {
        PVM_LimReadGradient            = 100.0;
        PVM_LimReadDephaseGradient     = 100.0;
        PVM_Lim2dPhaseGradient         = 100.0;
        PVM_Lim3dPhaseGradient         = 100.0;
        PVM_LimExSliceGradient         = 100.0;
        PVM_LimExSliceRephaseGradient  = 100.0;
    }
    else
    {
        PVM_LimReadGradient            = 57.0;
        PVM_LimReadDephaseGradient     = 70.7;
        PVM_Lim2dPhaseGradient         = 57.0;
        PVM_Lim3dPhaseGradient         = 57.0;
        PVM_LimExSliceGradient         = 100.0;
        PVM_LimExSliceRephaseGradient  = 70.7;
    }

}

/*jiadi xu add MT init and relation check*/

void MTRels(void)
{
    int i,inter_i;
    char line [256];
    FILE *fd;
    /*Initial all parameters*/
    if (!ParxRelsParHasValue("MTOffsetMode") )
    {
        // MTOffsetMode=Sequential_Offset;
        Number_Offsets=1;
        // Initial_MT_Offset=0;
        // Final_MT_Offset=0;
        // Offset_Step_Hz=0;
        MT_Offsets_NoM0[0]=0;

        // M0Option=NoM0;

    }
    M0Offset=0;

    Number_Offsets = MAX_OF(1,Number_Offsets);
    PARX_change_dims("MT_Offsets_NoM0",Number_Offsets);

    /*
    switch(MTOffsetMode)
    {
    case Sequential_Offset:
        Offset_Step_Hz=0;

        //***********Offset frequency list range check**********
        Initial_MT_Offset= MAX_OF(Initial_MT_Offset,-50000);
        Initial_MT_Offset = MIN_OF(Initial_MT_Offset,50000);
        Final_MT_Offset= MAX_OF(Final_MT_Offset,-50000);
        Final_MT_Offset = MIN_OF(Final_MT_Offset,50000);

        if (Number_Offsets > 1)
        {
            Offset_Step_Hz=(Final_MT_Offset-Initial_MT_Offset)/(Number_Offsets-1);
        }

        for ( i=0; i < Number_Offsets; i++)
        {
            MT_Offsets_NoM0[i] = Initial_MT_Offset+i*Offset_Step_Hz;
        }

        break;
    case Interleave_Offset:

        Offset_Step_Hz=0;
        //***********Offset frequency list range check**********
        Initial_MT_Offset= MAX_OF(Initial_MT_Offset,-50000);
        Initial_MT_Offset = MIN_OF(Initial_MT_Offset,50000);
        Final_MT_Offset= MAX_OF(Final_MT_Offset,-50000);
        Final_MT_Offset = MIN_OF(Final_MT_Offset,50000);

        if (Number_Offsets > 1)
        {
            Offset_Step_Hz=(Final_MT_Offset-Initial_MT_Offset)/(Number_Offsets-1);
        }

        for ( i=0; i < Number_Offsets; i++)
        {
            inter_i=2*i;
            if (inter_i>=Number_Offsets)
            {
                inter_i=2*Number_Offsets-inter_i-1;
            }
            MT_Offsets_NoM0[inter_i] = Initial_MT_Offset+i*Offset_Step_Hz;
        }

        break;

    case From_File:

        PARX_change_dims("MT_Offsets_NoM0",2000);
        i=0;
        MT_Offsets_NoM0[0]=0;
        if ((fd=fopen(MTListFile,"r"))!=NULL)
        {
            while(fgets(line,256,fd)!=NULL)
            {
                MT_Offsets_NoM0[i]=round(atof(line)*SFO1);
                i=i+1;
            }

            fclose(fd);

        }
        Number_Offsets=MAX_OF(i,1);
        PARX_change_dims("MT_Offsets_NoM0",Number_Offsets);
        break;

    case Enter_Manually:


        break;
    default:
        break;
    }
    */

    /***************************/
    /*
    switch(M0Option)
    {
    case M0_Last:
        Number_Offset_Experiments=Number_Offsets+1;
        PARX_change_dims("MT_Offsets",Number_Offset_Experiments);
        PARX_change_dims("M0Flag",Number_Offset_Experiments);

        for (i=0; i<Number_Offsets; i++)
        {
            MT_Offsets[i]=MT_Offsets_NoM0[i];
            M0Flag[i]=0;
        }
        MT_Offsets[Number_Offsets]=M0Offset;
        M0Flag[Number_Offsets]=1;

        break;
    case M0_First:
        Number_Offset_Experiments=Number_Offsets+1;
        PARX_change_dims("MT_Offsets",Number_Offset_Experiments);
        PARX_change_dims("M0Flag",Number_Offset_Experiments);

        for (i=1; i<Number_Offsets+1; i++)
        {
            MT_Offsets[i]=MT_Offsets_NoM0[i-1];
            M0Flag[i]=0;
        }
        MT_Offsets[0]=M0Offset;
        M0Flag[0]=1;

        break;
    default:
        Number_Offset_Experiments=Number_Offsets;
        PARX_change_dims("MT_Offsets",Number_Offset_Experiments);
        PARX_change_dims("M0Flag",Number_Offset_Experiments);

        for (i=0; i<Number_Offsets; i++)
        {
            MT_Offsets[i]=MT_Offsets_NoM0[i];
            M0Flag[i]=0;

        }

        break;
    }
    */
    /* added by HZeng */
    Number_Offset_Experiments = 1;
    PARX_change_dims("MT_Offsets",Number_Offset_Experiments);
    PARX_change_dims("M0Flag",Number_Offset_Experiments);
    MT_Offsets[0] = MT_Offsets_NoM0[0];
    M0Flag[0] = 0;
    /* end added by HZeng */

    PVM_NRepetitions= MAX_OF(Number_Offset_Experiments,1);
    /**************************/

    backbone();
}


/*HZeng cs init and relation check*/
void csRelations(void)
{
    initCSRel();
    readConfig();
    setupCSEnc();
    backbone();
}

void initCSRel(void) {
    printf("----> initCSRel\n");
    flagReadFile = 0;
    int i;
    int dim = PTB_GetSpatDim();
    strcpy(CS_MSG, "Please Select a config file");
    ParxRelsMakeNonEditable("CS_MSG, CS_EncMatrix, CS_ACT_EncMatrix, CS_Factor, CS_EncSteps_1, CS_EncSteps_2");
    PARX_change_dims("PVM_EncMatrix", dim);
    PARX_change_dims("CS_EncMatrix", dim);
    PARX_change_dims("CS_ACT_EncMatrix", dim);

    for (i = 1; i < dim; i++) {
        PVM_AntiAlias[i] = 1;
        PVM_EncMatrix[i] = PVM_Matrix[i];
    }
    for (i = 0; i < dim; i++) {
        CS_EncMatrix[i] = PVM_EncMatrix[i];
        CS_ACT_EncMatrix[i] = PVM_EncMatrix[i];
    }
    printf("<---- initCSRel\n");
}

void setupCSEnc(void) {
    int i;
    int dim = PTB_GetSpatDim();
    int encDim1 = CS_EncMatrix[1];
    int encDim2 = dim > 2 ? CS_EncMatrix[2] : 1;
    int actDim1 = CS_ACT_EncMatrix[1];
    int actDim2 = dim > 2 ? CS_ACT_EncMatrix[2] : 1;
    int totalEncSize = encDim1 * encDim2;
    CS_Factor = 1.0 * encDim1 * encDim2 / actDim1 / actDim2;
    PARX_change_dims("CS_EncValues_1", actDim1);
    PARX_change_dims("CS_EncValues_2", actDim2);
    PARX_change_dims("CS_EncSteps_1", totalEncSize);
    PARX_change_dims("CS_EncSteps_2", totalEncSize);
    PARX_change_dims("R2DLIST", totalEncSize);
    PARX_change_dims("R3DLIST", totalEncSize);

    for (i = 0; i < actDim1; i++) {
        CS_EncValues_1[i] = (2.0 * i - actDim1) / encDim1;
    }
    for (i = 0; i < actDim2; i++) {
        CS_EncValues_2[i] = (2.0 * i - actDim2) / encDim2;
    }
    if (flagReadFile == 0) {
        for (i = 0; i < totalEncSize; i++) {
            CS_EncSteps_1[i] = i % encDim1 - encDim1 / 2;
            CS_EncSteps_2[i] = i / encDim1 - encDim2 / 2;
            R2DLIST[i] = i % encDim1;
            R3DLIST[i] = i / encDim1;
        }
    }
    else {
        readEnc();
    }
}

void readConfig(void) {
    printf("----> readConfig\n");
    flagReadFile = 0;
    int dim = PTB_GetSpatDim();
    char line [1024], str1[1024];
    FILE *fd;
    int n1, n2, n3, n4, n5;

    printf("read CSFile\n");
    fd = fopen(CS_config, "r");
    if (fd != NULL) {
        printf("open CSFile successfully!\n");

        fgets(line, 1024, fd);
        fgets(line, 1024, fd);
        // read CS_EncMatrix
        if (dim == 2) {
            sscanf(line, "%s %d", str1, &n1);
            printf("%d\n", n1);
        }
        if (dim == 3) {
            sscanf(line, "%s %d %d", str1, &n1, &n2);
            printf("%d %d\n", n1, n2);
        }
        if (n1 != CS_EncMatrix[1] || (dim == 3 && n2 != CS_EncMatrix[2])) {
            strcpy(CS_MSG, "Encoding Size doesn't match");
            return;
        }

        fgets(line, 1024, fd);
        // read CS_ACT_EncMatrix
        if (dim == 2) {
            sscanf(line, "%s %d", str1, &n3);
        }
        if (dim == 3) {
            sscanf(line, "%s %d %d", str1, &n3, &n4);
        }
        if (n3 < CS_EncMatrix[1] || (dim == 3 && n4 < CS_EncMatrix[2])) {
            strcpy(CS_MSG, "CS_ACT_EncMatrix can not be smaller than CS_EncMatrix");
            return;
        }
        CS_ACT_EncMatrix[1] = n3;
        if (dim ==3) {
            CS_ACT_EncMatrix[2] = n4;
        }

        fgets(line, 1024, fd);
        // read PVM_RareFactor
        sscanf(line, "%s %d", str1, &n5);
        if (n5 != PVM_RareFactor) {
            strcpy(CS_MSG, "PVM_RareFactor does not match");
            return;
        }
        fclose(fd);
        flagReadFile = 1;
    }
    else
    {
        printf("fail to open CSFile!\n");
    }
    printf("<---- readConfig\n");
}

void readEnc(void) {
    printf("----> readEnc\n");
    int i;
    int dim = PTB_GetSpatDim();
    char line [1024];
    FILE *fd;
    int n1, n2;
    int totalEncSize = CS_EncMatrix[1];
    if (dim == 3) {
        totalEncSize *= CS_EncMatrix[2];
    }
    if (flagReadFile == 1) {
        fd = fopen(CS_config, "r");
        if (fd != NULL) {
            printf("open CSFile successfully!\n");
            for (i =0; i < 4; i++) {
                fgets(line, 1024, fd);
            }
            i = 0;
            while( fgets(line, 1024, fd) != NULL && i < totalEncSize) {
                if (dim == 2) {
                    sscanf(line, "%d", &n1);
                }
                if (dim == 3) {
                    sscanf(line, "%d %d", &n1, &n2);
                }
                CS_EncSteps_1[i] = n1;
                R2DLIST[i] = n1 + CS_ACT_EncMatrix[1] / 2;
                if (dim == 3) {
                    CS_EncSteps_2[i] = n2;
                    R3DLIST[i] = n2 + CS_ACT_EncMatrix[2] / 2;
                }
                i++;
            }
            sprintf(CS_MSG, "Successfully Load %s", CS_config);
            fclose(fd);
        }
    }
    else {
        printf("Config is not correct\n");
    }
    printf("<---- readEnc\n");
}


/****************************************************************/
/*		E N D   O F   F I L E				*/
/****************************************************************/

