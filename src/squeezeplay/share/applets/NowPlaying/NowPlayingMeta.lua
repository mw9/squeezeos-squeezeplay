local oo            = require("loop.simple")

local AppletMeta    = require("jive.AppletMeta")

local appletManager = appletManager
local jiveMain      = jiveMain


module(...)
oo.class(_M, AppletMeta)


function jiveVersion(self)
	return 1, 1
end

function defaultSettings(self)
	return {
    		screensaverArtworkSize = "ss"
	}
end

function registerApplet(self)

	self:registerService('goNowPlaying')
	self:registerService("hideNowPlaying")

end


function configureApplet(self)

	appletManager:callService("addScreenSaver",
		self:string("SCREENSAVER_NOWPLAYING"), 
		"NowPlaying", 
		"openScreensaver", 
		_,
		_,
		10
	)

	-- NowPlaying is a resident applet
	appletManager:loadApplet("NowPlaying")

end

