

#include "chuck_dl.h"
#include "chuck_def.h"

#include <stdio.h>
#include <limits.h>
#include <math.h>

#include "Biquad.h"
#include "Filters.h"
#include "dsp.h"


CK_DLL_CTOR(absaturator_ctor);
CK_DLL_DTOR(absaturator_dtor);

CK_DLL_MFUN(absaturator_setDrive);
CK_DLL_MFUN(absaturator_getDrive);
CK_DLL_MFUN(absaturator_setDCOffset);
CK_DLL_MFUN(absaturator_getDCOffset);

CK_DLL_TICK(absaturator_tick);

t_CKINT absaturator_data_offset = 0;


const static double AACoefs[6][5] =
{
    /*** type = cheby2, order = 12, cutoff = 0.078125 ***/
    {2.60687e-05, 2.98697e-05, 2.60687e-05, -1.31885, 0.437162},
    {1, -0.800256, 1, -1.38301, 0.496576},
    {1, -1.42083, 1, -1.48787, 0.594413},
    {1, -1.6374, 1, -1.60688, 0.707142},
    {1, -1.7261, 1, -1.7253, 0.822156},
    {1, -1.75999, 1, -1.84111, 0.938811}
};

class ABSaturator
{
public:
    
    ABSaturator(float fs)
    {
        m_drive = 1;
        m_dcOffset = 0;
        
        for(int j = 0; j < kAAOrder; j++)
        {
            AIFilter[j].setCoefs((double *) AACoefs[j]);
            AAFilter[j].setCoefs((double *) AACoefs[j]);
        }
        
        double wc_dc = 5*2*M_PI;
        //                           b0 b1 b2     a0 a1 a2
        double dcblockScoeffs[6] = {  0, 1, 0, wc_dc, 1, 0 };
        double dcblockZcoeffs[5];
        Filters::bilinearTranform(dcblockScoeffs, dcblockZcoeffs, fs*kUSRatio);
        dcBlocker[0].setCoefs(dcblockZcoeffs);
        dcBlocker[1].setCoefs(dcblockZcoeffs);
    }
    
    SAMPLE tick(SAMPLE in)
    {
        double isignal, fsignal, osignal, usignal, dsignal;
        
        fsignal = m_drive*in;
        
		// upsample, apply distortion, downsample
		for(int k = 0; k < kUSRatio; k++)
        {
			// upsample (insert zeros)
            usignal = (k == 0) ? kUSRatio*fsignal : 0.0;
            
			// apply antiimaging filter
			for(int j = 0; j < kAAOrder; j++)
				AIFilter[j].process(usignal,usignal);
            
			// apply distortion
			// note: x / (1+|x|) gives a soft saturation
			// where as min(1, max(-1, x)) gives a hard clipping
			//dsignal = usignal / (1.0 + fabs(usignal));
            //dsignal = min(1.0, max(-1.0, usignal));
            dsignal = (usignal + m_dcOffset) / (1.0 + fabs(usignal + m_dcOffset));
            
            dcBlocker[0].process(dsignal, dsignal);
            dcBlocker[1].process(dsignal, dsignal);
            
			// apply antialiasing filter
			for(int j = 0; j < kAAOrder; j++)
				AAFilter[j].process(dsignal,dsignal);
		}
        
        return dsignal;
    }
    
    float setDrive(float d)
    {
        m_drive = dB2lin(d);
        return m_drive;
    }
    
    float getDrive() { return m_drive; }
    
    float setDCOffset(float d)
    {
        m_dcOffset = d;
        return m_dcOffset;
    }
    
    float getDCOffset() { return m_dcOffset; }
    
private:
    
    float m_drive;
    
    float inputFilterGain;
    float inputFilterCutoff;
    float inputFilterQ;
    
    float outputFilterGain;
    float outputFilterCutoff;
    float outputFilterQ;
    
    float m_dcOffset;
    
    double InCoefs[5];	// input filter coefficients
	Biquad InFilter;	// input filter
    
	double OutCoefs[5];	// input filter coefficients
	Biquad OutFilter;	// output filter
    
    Biquad dcBlocker[2];
    
	enum{kUSRatio = 8};	// upsampling factor, sampling rate ratio
	enum{kAAOrder = 6};	// antialiasing/antiimaging filter order, biquads
	Biquad AIFilter[kAAOrder];	// antiimaging filter
	Biquad AAFilter[kAAOrder];	// antialiasing filter
    
};

CK_DLL_QUERY(ABSaturator)
{
    QUERY->setname(QUERY, "ABSaturator");
    
    QUERY->begin_class(QUERY, "ABSaturator", "UGen");
    
    QUERY->add_ctor(QUERY, absaturator_ctor);
    QUERY->add_dtor(QUERY, absaturator_dtor);
    
    QUERY->add_ugen_func(QUERY, absaturator_tick, NULL, 1, 1);
    
    QUERY->add_mfun(QUERY, absaturator_setDrive, "float", "drive");
    QUERY->add_arg(QUERY, "float", "arg");
    
    QUERY->add_mfun(QUERY, absaturator_getDrive, "float", "drive");
    
    QUERY->add_mfun(QUERY, absaturator_setDCOffset, "float", "dcOffset");
    QUERY->add_arg(QUERY, "float", "arg");
    
    QUERY->add_mfun(QUERY, absaturator_getDCOffset, "float", "dcOffset");
    
    absaturator_data_offset = QUERY->add_mvar(QUERY, "int", "@sat_data", false);
    
    QUERY->end_class(QUERY);

    return TRUE;
}


CK_DLL_CTOR(absaturator_ctor)
{
    OBJ_MEMBER_INT(SELF, absaturator_data_offset) = 0;
    
    ABSaturator * bcdata = new ABSaturator(API->vm->get_srate());
    
    OBJ_MEMBER_INT(SELF, absaturator_data_offset) = (t_CKINT) bcdata;
}

CK_DLL_DTOR(absaturator_dtor)
{
    ABSaturator * bcdata = (ABSaturator *) OBJ_MEMBER_INT(SELF, absaturator_data_offset);
    if(bcdata)
    {
        delete bcdata;
        OBJ_MEMBER_INT(SELF, absaturator_data_offset) = 0;
        bcdata = NULL;
    }
}

CK_DLL_TICK(absaturator_tick)
{
    ABSaturator * s = (ABSaturator *) OBJ_MEMBER_INT(SELF, absaturator_data_offset);
    
    if(s)
        *out = s->tick(in);

    return TRUE;
}

CK_DLL_MFUN(absaturator_setDrive)
{
    ABSaturator * bcdata = (ABSaturator *) OBJ_MEMBER_INT(SELF, absaturator_data_offset);
    // TODO: sanity check
    bcdata->setDrive(GET_NEXT_FLOAT(ARGS));
    RETURN->v_float = bcdata->getDrive();
}

CK_DLL_MFUN(absaturator_getDrive)
{
    ABSaturator * bcdata = (ABSaturator *) OBJ_MEMBER_INT(SELF, absaturator_data_offset);
    RETURN->v_float = bcdata->getDrive();
}

CK_DLL_MFUN(absaturator_setDCOffset)
{
    ABSaturator * bcdata = (ABSaturator *) OBJ_MEMBER_INT(SELF, absaturator_data_offset);
    // TODO: sanity check
    bcdata->setDCOffset(GET_NEXT_FLOAT(ARGS));
    RETURN->v_float = bcdata->getDCOffset();
}

CK_DLL_MFUN(absaturator_getDCOffset)
{
    ABSaturator * bcdata = (ABSaturator *) OBJ_MEMBER_INT(SELF, absaturator_data_offset);
    RETURN->v_float = bcdata->getDCOffset();
}

