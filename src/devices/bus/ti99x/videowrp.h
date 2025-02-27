// license:LGPL-2.1+
// copyright-holders:Michael Zapf
/****************************************************************************

    TI-99/4(A) and /8 Video subsystem
    See videowrp.c for documentation

    Michael Zapf
    October 2010
    January 2012: Rewritten as class

*****************************************************************************/

#ifndef __TIVIDEO__
#define __TIVIDEO__

#include "video/tms9928a.h"
#include "video/v9938.h"
#include "ti99defs.h"
#include "sound/sn76496.h"

class ti_video_device : public bus8z_device
{
public:
	virtual void    reset_vdp(int state) =0;

protected:
	tms9928a_device *m_tms9928a;

	/* Constructor */
	ti_video_device(const machine_config &mconfig, device_type type, const char *name, const char *tag, device_t *owner, UINT32 clock, const char *shortname, const char *source);
	virtual void device_start(void) override;
	virtual void device_reset(void) override { };
	virtual DECLARE_READ8Z_MEMBER(readz) override { };
	virtual DECLARE_WRITE8_MEMBER(write) override { };
};

/*
    Used in the TI-99/4A and TI-99/8
*/
class ti_std_video_device : public ti_video_device
{
public:
	ti_std_video_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock);
	DECLARE_READ8Z_MEMBER(readz) override;
	DECLARE_WRITE8_MEMBER(write) override;

	void    reset_vdp(int state) override { m_tms9928a->reset_line(state); }
};

/*
    Used in the EVPC and Geneve
*/
class ti_exp_video_device : public ti_video_device
{
public:
	ti_exp_video_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock);
	template<class _Object> static devcb_base &set_out_gromclk_callback(device_t &device, _Object object) { return downcast<ti_exp_video_device &>(device).m_out_gromclk_cb.set_callback(object); }

	void video_update_mouse(int delta_x, int delta_y, int buttons);
	DECLARE_READ8Z_MEMBER(readz) override;
	DECLARE_WRITE8_MEMBER(write) override;
	DECLARE_READ16_MEMBER(read16);
	DECLARE_WRITE16_MEMBER(write16);
	void    reset_vdp(int state) override { m_v9938->reset_line(state); }

protected:
	virtual void        device_start(void) override;
	virtual void device_reset(void) override;

private:
	void device_timer(emu_timer &timer, device_timer_id id, int param, void *ptr) override;
	v9938_device        *m_v9938;
	devcb_write_line    m_out_gromclk_cb; // GROMCLK line is optional; if present, pulse it by XTAL/24 rate
	emu_timer   *m_gromclk_timer;
};

#define MCFG_ADD_GROMCLK_CB(_devcb) \
	devcb = &ti_exp_video_device::set_out_gromclk_callback(*device, DEVCB_##_devcb);


extern const device_type TI99VIDEO;
extern const device_type V9938VIDEO;

/****************************************************************************/
/*
    Sound device
*/

extern const device_type TISOUND_94624;
extern const device_type TISOUND_76496;

#define TI_SOUND_CONFIG(name) \
	const ti_sound_config(name) =

class ti_sound_system_device : public bus8z_device
{
public:
	ti_sound_system_device(const machine_config &mconfig, device_type type, const char *name, const char *tag, device_t *owner, UINT32 clock, const char *shortname, const char *source)
	: bus8z_device(mconfig, type, name, tag, owner, clock, shortname, source), m_sound_chip(nullptr),
		m_console_ready(*this) { };

	// Cannot read from sound; just ignore silently
	DECLARE_READ8Z_MEMBER(readz) override { };
	DECLARE_WRITE8_MEMBER(write) override;
	DECLARE_WRITE_LINE_MEMBER( sound_ready );   // connect to console READY

	template<class _Object> static devcb_base &static_set_int_callback(device_t &device, _Object object) { return downcast<ti_sound_system_device &>(device).m_console_ready.set_callback(object); }

protected:
	virtual void device_start(void) override;
	virtual machine_config_constructor device_mconfig_additions() const override  =0;

private:
	sn76496_base_device*    m_sound_chip;
	devcb_write_line       m_console_ready;
};

/*
    The version that sits in the TI-99/4A
*/
class ti_sound_sn94624_device : public ti_sound_system_device
{
public:
	ti_sound_sn94624_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock);

protected:
	virtual machine_config_constructor device_mconfig_additions() const override;
};

/*
    The version that sits in the TI-99/8 and Geneve
*/
class ti_sound_sn76496_device : public ti_sound_system_device
{
public:
	ti_sound_sn76496_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock);

protected:
	virtual machine_config_constructor device_mconfig_additions() const override;
};


/****************************************************************************/

#define MCFG_TI_TMS991x_ADD_NTSC(_tag, _chip, _vsize, _class, _int, _gclk)    \
	MCFG_DEVICE_ADD(_tag, TI99VIDEO, 0)                                     \
	MCFG_DEVICE_ADD( VDP_TAG, _chip, XTAL_10_738635MHz / 2 )                  \
	MCFG_TMS9928A_VRAM_SIZE(_vsize) \
	MCFG_TMS9928A_OUT_INT_LINE_CB(WRITELINE(_class,_int)) \
	MCFG_TMS9928A_OUT_GROMCLK_CB(WRITELINE(_class,_gclk)) \
	MCFG_TMS9928A_SCREEN_ADD_NTSC( SCREEN_TAG )                             \
	MCFG_SCREEN_UPDATE_DEVICE( VDP_TAG, tms9928a_device, screen_update )

#define MCFG_TI_TMS991x_ADD_PAL(_tag, _chip, _vsize, _class, _int, _gclk)     \
	MCFG_DEVICE_ADD(_tag, TI99VIDEO, 0)                                     \
	MCFG_DEVICE_ADD( VDP_TAG, _chip, XTAL_10_738635MHz / 2 ) \
	MCFG_TMS9928A_VRAM_SIZE(_vsize) \
	MCFG_TMS9928A_OUT_INT_LINE_CB(WRITELINE(_class,_int))   \
	MCFG_TMS9928A_OUT_GROMCLK_CB(WRITELINE(_class,_gclk)) \
	MCFG_TMS9928A_SCREEN_ADD_PAL( SCREEN_TAG )                              \
	MCFG_SCREEN_UPDATE_DEVICE( VDP_TAG, tms9928a_device, screen_update )

#define MCFG_TI998_ADD_NTSC(_tag, _chip, _vsize, _class, _int, _gclk) \
	MCFG_DEVICE_ADD(_tag, TI99VIDEO, 0)                                     \
	MCFG_DEVICE_ADD( VDP_TAG, _chip, XTAL_10_738635MHz / 2 )                  \
	MCFG_TMS9928A_VRAM_SIZE(_vsize) \
	MCFG_TMS9928A_OUT_INT_LINE_CB(WRITELINE(_class,_int)) \
	MCFG_TMS9928A_OUT_GROMCLK_CB(WRITELINE(_class,_gclk)) \
	MCFG_TMS9928A_SCREEN_ADD_NTSC( SCREEN_TAG )                             \
	MCFG_SCREEN_UPDATE_DEVICE( VDP_TAG, tms9928a_device, screen_update )

#define MCFG_TI998_ADD_PAL(_tag, _chip, _vsize, _class, _int, _gclk)      \
	MCFG_DEVICE_ADD(_tag, TI99VIDEO, 0)                                     \
	MCFG_DEVICE_ADD( VDP_TAG, _chip, XTAL_10_738635MHz / 2 )                      \
	MCFG_TMS9928A_VRAM_SIZE(_vsize) \
	MCFG_TMS9928A_OUT_INT_LINE_CB(WRITELINE(_class,_int)) \
	MCFG_TMS9928A_OUT_GROMCLK_CB(WRITELINE(_class,_gclk)) \
	MCFG_TMS9928A_SCREEN_ADD_PAL( SCREEN_TAG )                              \
	MCFG_SCREEN_UPDATE_DEVICE( VDP_TAG, tms9928a_device, screen_update )

#define MCFG_TI_SOUND_94624_ADD(_tag)            \
	MCFG_DEVICE_ADD(_tag, TISOUND_94624, 0)

#define MCFG_TI_SOUND_76496_ADD(_tag)            \
	MCFG_DEVICE_ADD(_tag, TISOUND_76496, 0)

#define MCFG_TI_SOUND_READY_HANDLER( _ready ) \
	devcb = &ti_sound_system_device::static_set_int_callback( *device, DEVCB_##_ready );

#endif /* __TIVIDEO__ */
