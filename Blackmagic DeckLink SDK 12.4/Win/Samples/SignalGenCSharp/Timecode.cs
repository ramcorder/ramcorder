/* -LICENSE-START-
** Copyright (c) 2020 Blackmagic Design
**  
** Permission is hereby granted, free of charge, to any person or organization 
** obtaining a copy of the software and accompanying documentation (the 
** "Software") to use, reproduce, display, distribute, sub-license, execute, 
** and transmit the Software, and to prepare derivative works of the Software, 
** and to permit third-parties to whom the Software is furnished to do so, in 
** accordance with:
** 
** (1) if the Software is obtained from Blackmagic Design, the End User License 
** Agreement for the Software Development Kit (“EULA”) available at 
** https://www.blackmagicdesign.com/EULA/DeckLinkSDK; or
** 
** (2) if the Software is obtained from any third party, such licensing terms 
** as notified by that third party,
** 
** and all subject to the following:
** 
** (3) the copyright notices in the Software and this entire statement, 
** including the above license grant, this restriction and the following 
** disclaimer, must be included in all copies of the Software, in whole or in 
** part, and all derivative works of the Software, unless such copies or 
** derivative works are solely in the form of machine-executable object code 
** generated by a source language processor.
** 
** (4) THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS 
** OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
** FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT 
** SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE 
** FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE, 
** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
** DEALINGS IN THE SOFTWARE.
** 
** A copy of the Software is available free of charge at 
** https://www.blackmagicdesign.com/desktopvideo_sdk under the EULA.
** 
** -LICENSE-END-
*/

namespace SignalGenCSharp
{
	class Timecode
	{
		ulong FrameCount { get; }
		uint FPS { get; }
		uint DropFrames { get; }

		public byte Hours { get; }
		public byte Minutes { get; }
		public byte Seconds { get; }
		public byte Frames { get; }

		public Timecode(ulong frameCount, uint fps, uint dropFrames)
		{
			FrameCount = frameCount;
			FPS = fps;
			DropFrames = dropFrames;

			ulong frameCountNormalized = FrameCount;

			if (dropFrames != 0)
			{
				uint deciMins;
				uint deciMinsRemainder;

				uint framesIn10mins = (60 * 10 * FPS) - (9 * DropFrames);
				deciMins = (uint)(frameCountNormalized / framesIn10mins);
				deciMinsRemainder = (uint)(frameCountNormalized - (deciMins * framesIn10mins));

				// Add drop frames for 9 minutes of every 10 minutes that have elapsed
				// AND drop frames for every minute (over the first minute) in this 10-minute block.
				frameCountNormalized += DropFrames * 9 * deciMins;
				if (deciMinsRemainder >= DropFrames)
					frameCountNormalized += DropFrames * ((deciMinsRemainder - DropFrames) / (framesIn10mins / 10));
			}

			Frames = (byte)(frameCountNormalized % fps);
			frameCountNormalized /= fps;
			Seconds = (byte)(frameCountNormalized % 60);
			frameCountNormalized /= 60;
			Minutes = (byte)(frameCountNormalized % 60);
			frameCountNormalized /= 60;
			Hours = (byte)frameCountNormalized;
		}

		public static Timecode operator ++(Timecode timecode)
		{
			return new Timecode(timecode.FrameCount + 1, timecode.FPS, timecode.DropFrames);
		}
	}
}
