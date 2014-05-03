/****************************************************************************
 *
 * MODULE:             JIP Web Apps
 *
 * COMPONENT:          JIP Javascript wrapper
 *
 * REVISION:           $Revision: 43426 $
 *
 * DATED:              $Date: 2012-06-18 15:24:02 +0100 (Mon, 18 Jun 2012) $
 *
 * AUTHOR:             Matt Redfearn
 *
 ****************************************************************************
 *
 * This software is owned by NXP B.V. and/or its supplier and is protected
 * under applicable copyright laws. All rights are reserved. We grant You,
 * and any third parties, a license to use this software solely and
 * exclusively on NXP products [NXP Microcontrollers such as JN5148, JN5142, JN5139]. 
 * You, and any third parties must reproduce the copyright and warranty notice
 * and any other legend of ownership on each copy or partial copy of the 
 * software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.

 * Copyright NXP B.V. 2012. All rights reserved
 *
 ***************************************************************************/

/** Placeholder for JenNet-IP available Border routers */
var BRList=[];

/** Placeholder for JenNet-IP Border router address */
var ActiveBorderRouter = ""

/** Placeholder for JenNet-IP network contents */
var Network=[];


/** jQuery queue based manager for ajax requests. */
var JIP_AjaxManager = (function() {
     var requests = [];

     return {
        addReq:  function(opt) {
            requests.push(opt);
        },
        removeReq:  function(opt) {
            if( $.inArray(opt, requests) > -1 )
                requests.splice($.inArray(opt, requests), 1);
        },
        run: function() {
            var self = this,
                orgSuc;

            if( requests.length ) {
                oriSuc = requests[0].complete;

                requests[0].complete = function() {
                     if( typeof oriSuc === 'function' ) oriSuc();
                     requests.shift();
                     self.run.apply(self, []);
                };   

                $.ajax(requests[0]);
            } else {
              self.tid = setTimeout(function() {
                 self.run.apply(self, []);
              }, 100);
            }
        },
        stop:  function() {
            requests = [];
            clearTimeout(this.tid);
        },
        clear: function() {
            requests = [];
        }
     };
}());
JIP_AjaxManager.run();

function JIP_CancelPendingRequests()
{
    JIP_AjaxManager.clear();
}

function JIP_Request(request, callback)
{
    JIP_AjaxManager.addReq({
        type: 'POST',
        url: '/cgi-bin/JIP.cgi',
        data: request,
        success: callback
    });
    return;
}


function JIP_GetVersion(callback) 
{ 
    var request; 
    request = "action=getVersion";
    
    JIP_Request(request, function(Result) {
        callback(Result);
    });
}


function JIP_DiscoverBRs(callback) 
{ 
    var request; 
    request = "action=discoverBRs";
    
    JIP_Request(request, function(Result) {
        BRList = Result.BRList;
        callback(Result.Status);
    });
}

function JIP_Discover(callback, IPv6Address) 
{ 
    if (IPv6Address != undefined)
    {
        ActiveBorderRouter = IPv6Address;
        var request;
        request = "action=discover&BRaddress=" + ActiveBorderRouter;
        
        JIP_Request(request, function(Result) {
            Network = Result.Network;
            if (callback)
            {
                callback(Result.Status);
            }
        });
    }
}


function JIP_GetVar(address, mib, variable, callback, user) 
{ 
    var request; 
    request = "action=GetVar&BRaddress=" + ActiveBorderRouter;;
    request = request + "&nodeaddress=" + address;
    request = request + "&mib=" + mib;
    request = request + "&var=" + variable; 
    request = request + "&refresh=no"; 
    
    JIP_Request(request, function(Result) {
        var Network = Result.Network
        var Nodes = Network["Nodes"];
        if (Nodes.length == 0)
        {
            callback(0xFF, user, "?");
            return;
        }
        var NewValue = Network["Nodes"][0]["MiBs"][0]["Vars"][0]["Value"];
        if (NewValue)
        {
            NewValue = NewValue.toString();
        }
        callback(Result.Status, user, NewValue);
    });
}


function JIP_SetVar(address, mib, variable, value, callback, user) 
{ 
    var request; 
    request = "action=SetVar&BRaddress=" + ActiveBorderRouter;
    request = request + "&nodeaddress=" + address;
    request = request + "&mib=" + mib;
    request = request + "&var=" + variable; 
    request = request + "&value=" + value; 
    request = request + "&refresh=no"; 
    
    JIP_Request(request, function(Result) {
        callback(Result.Status, user);
    });
}

