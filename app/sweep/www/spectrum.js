/*
 * Copyright (c) 2019 Jeppe Ledet-Pedersen
 * This software is released under the MIT license.
 * See the LICENSE file for further details.
 */

'use strict';

Spectrum.prototype.squeeze = function(value, out_min, out_max) {
    if (value <= this.min_db)
        return out_min;
    else if (value >= this.max_db)
        return out_max;
    else
        return Math.round((value - this.min_db) / (this.max_db - this.min_db) * out_max);
}

Spectrum.prototype.rowToImageData = function(bins) {
    for (var i = 0; i < this.imagedata.data.length; i += 4) {
        var cindex = this.squeeze(bins[i/4], 0, 255);
        var color = this.colormap[cindex];
        this.imagedata.data[i+0] = color[0];
        this.imagedata.data[i+1] = color[1];
        this.imagedata.data[i+2] = color[2];
        this.imagedata.data[i+3] = 255;
    }
}

Spectrum.prototype.addWaterfallRow = function(bins) {
    // Shift waterfall 1 row down
    this.ctx_wf.drawImage(this.ctx_wf.canvas,
        0, 0, this.wf_size, this.wf_rows - 1,
        0, 1, this.wf_size, this.wf_rows - 1);
    
    this.wfrowcount++;

    // Draw new line on waterfall canvas
    this.rowToImageData(bins);
    this.ctx_wf.putImageData(this.imagedata, 0, 0);

    if (this.wfrowcount % 100 == 0)
    {
        var timeString = new Date().toLocaleTimeString();
        this.ctx_wf.font = "30px sans-serif";
        this.ctx_wf.fillStyle = "white";
        this.ctx_wf.textBaseline = "top";
        this.ctx_wf.fillText(timeString, 0, 0); // TODO: Fix font scaling
    }

    var width = this.ctx.canvas.width;
    var height = this.ctx.canvas.height;

    // Copy scaled FFT canvas to screen. Only copy the number of rows that will
    // fit in waterfall area to avoid vertical scaling.
    this.ctx.imageSmoothingEnabled = false;
    var rows = Math.min(this.wf_rows, height - this.spectrumHeight);
    /*
    this.ctx.drawImage(this.ctx_wf.canvas,
        this.xoffset, 0, this.wf_size/this.zoom, rows,
        0, this.spectrumHeight, width, height - this.spectrumHeight);
    */
        this.ctx.setTransform((this.zoom) ,0,0,1,this.pos-this.xoffset*this.zoom,0);
        this.ctx.drawImage(this.ctx_wf.canvas,
            0, 0,  this.wf_size, rows,
            0, this.spectrumHeight, this.wf_size, height - this.spectrumHeight);
            this.ctx.setTransform(1,0,0,1,0,0);    
    //    this.ctx.setTransform((this.zoom) ,0,0,1,this.pos-this.xoffset*this.zoom,0);
}

Spectrum.prototype.drawFFT = function(bins) {
    this.ctx.beginPath();
    this.ctx.moveTo(-1, this.spectrumHeight + 1);
    for (var i = 0; i < bins.length; i++) {
        var y = this.spectrumHeight - this.squeeze(bins[i], 0, this.spectrumHeight);
        if (y > this.spectrumHeight - 1)
            y = this.spectrumHeight + 1; // Hide underflow
        if (y < 0)
            y = 0;
        if (i == 0)
            this.ctx.lineTo(-1, y);
        this.ctx.lineTo(i, y);
        if (i == bins.length - 1)
            this.ctx.lineTo(this.wf_size + 1, y);
    }
    this.ctx.lineTo(this.wf_size + 1, this.spectrumHeight + 1);
    this.ctx.strokeStyle = "#fefefe";
    this.ctx.stroke();
    //this.ctx.drawImage(this.ctx.canvas,
    //    this.xoffset, 0, this.wf_size, this.spectrumHeight + 1,
    //    0, this.spectrumHeight, width, height - this.spectrumHeight);
    //this.ctx.scale((width*this.zoom) / this.wf_size, 1);
}

Spectrum.prototype.drawSpectrum = function(bins) {
    var width = this.ctx.canvas.width;
    var height = this.ctx.canvas.height;

    // Fill with black
    this.ctx.fillStyle = "black";
    this.ctx.fillRect(0, 0, width, height);

    // FFT averaging
    if (this.averaging > 0) {
        if (!this.binsAverage || this.binsAverage.length != bins.length) {
            this.binsAverage = Array.from(bins);
        } else {
            for (var i = 0; i < bins.length; i++) {
                this.binsAverage[i] += this.alpha * (bins[i] - this.binsAverage[i]);
            }
        }
        bins = this.binsAverage;
    }

    // Max hold
    if (this.maxHold) {
        if (!this.binsMax || this.binsMax.length != bins.length) {
            this.binsMax = Array.from(bins);
        } else {
            for (var i = 0; i < bins.length; i++) {
                if (bins[i] > this.binsMax[i]) {
                    this.binsMax[i] = bins[i];
                } else {
                    // Decay
                    this.binsMax[i] = 1.0025 * this.binsMax[i];
                }
            }
        }
    }

    // Do not draw anything if spectrum is not visible
    if (this.ctx_axes.canvas.height < 1)
        return;

    // Scale for FFT
    //this.ctx.save();
    //this.offset=-(32*4096)/(this.zoom*2);
    //this.xoffset=(this.wf_size)/(2*this.zoom);
    //this.xoffset= (this.wf_size/(2*this.zoom));
    //this.ctx.translate(-this.xoffset, this.yoffset);
    
   //this.ctx.scale((width*this.zoom) / this.wf_size, 1);
    //this.ctx.translate(-(this.xoffset), this.yoffset);

    //this.ctx.setTransform((width*this.zoom) / this.wf_size,0,0,1,-(this.xoffset),0);
    this.ctx.setTransform((this.zoom) ,0,0,1,this.pos-this.xoffset*this.zoom,0);
    
    // Draw maxhold
    if (this.maxHold)
        this.drawFFT(this.binsMax);

    // Do autoscale axes
    if (this.autoScale)
        this.doAutoScale(bins);

    // Draw FFT bins
    this.drawFFT(bins);

    // Restore scale
    //this.ctx.restore();
    this.ctx.setTransform(1,0,0,1,0,0);

    // Fill scaled path
    this.ctx.fillStyle = this.gradient;
    this.ctx.fill();

    // Copy axes from offscreen canvas
    this.ctx.drawImage(this.ctx_axes.canvas, 0, 0);
}

Spectrum.prototype.updateInfo = function(x) {
    var width = this.ctx_axes.canvas.width;
    //var height = this.ctx_InfoFrequency.canvas.height;

    // Clear text canvas
    var width_text = this.ctx_InfoFrequency.canvas.width;
    var height_text= this.InfoFrequency.height;

    this.ctx_InfoFrequency.clearRect(0, 0, width_text, 50+height_text);

    // Draw axes
    this.ctx_InfoFrequency.font = "12px sans-serif";
    this.ctx_InfoFrequency.fillStyle = "white";
    this.ctx_InfoFrequency.textBaseline = "middle";

    this.ctx_InfoFrequency.textAlign = "left";
    
    var freq = (((this.Screentobin(x)-this.wf_size/2)/this.wf_size*this.NativeSpan)+this.orginalfreq);
    if (this.centerHz + this.spanHz > 1e6)
        freq = (freq / 1e6).toFixed(3) + "M";
    else if (this.centerHz + this.spanHz/this.zoom > 1e3)
        freq = (freq / 1e3).toFixed(3) + "k";
    
    this.ctx_InfoFrequency.fillText(freq, x, 50);
    
}

Spectrum.prototype.updateAxes = function() {
    var width = this.ctx_axes.canvas.width;
    var height = this.ctx_axes.canvas.height;

    // Clear axes canvas
    
    this.ctx_axes.clearRect(0, 0, width, height);

    // Draw axes
    this.ctx_axes.font = "12px sans-serif";
    this.ctx_axes.fillStyle = "white";
    this.ctx_axes.textBaseline = "middle";

    this.ctx_axes.textAlign = "left";
    var step = 100;
    
    for (var i = this.min_db + 10; i <= this.max_db - 10; i += step) {
        var y = height - this.squeeze(i, 0, height);
        this.ctx_axes.fillText(i, 5, y);

        this.ctx_axes.beginPath();
        this.ctx_axes.moveTo(20, y);
        this.ctx_axes.lineTo(width, y);
        this.ctx_axes.strokeStyle = "rgba(200, 200, 200, 0.10)";
        this.ctx_axes.stroke();
    }

    this.ctx_axes.textBaseline = "bottom";
    for (var i = 0; i < 11; i++) {
        var x = Math.round(width / 10) * i;

        if (this.spanHz > 0) {
            var adjust = 0;
            if (i == 0) {
                this.ctx_axes.textAlign = "left";
                adjust = 3;
            } else if (i == 10) {
                this.ctx_axes.textAlign = "right";
                adjust = -3;
            } else {
                this.ctx_axes.textAlign = "center";
            }

            var freq = this.centerHz + (this.spanHz)/ 10 * (i - 5);
            if (this.centerHz + this.spanHz > 1e6)
                freq = (freq / 1e6).toFixed(3) + "M";
            else if (this.centerHz + this.spanHz > 1e3)
                freq = (freq / 1e3).toFixed(3) + "k";
            this.ctx_axes.fillText(freq, x + adjust, height - 3);
        }

        this.ctx_axes.beginPath();
        this.ctx_axes.moveTo(x, 0);
        this.ctx_axes.lineTo(x, height);
        this.ctx_axes.strokeStyle = "rgba(200, 200, 200, 0.10)";
        this.ctx_axes.stroke();
    
    }
}

Spectrum.prototype.addData = function(data) {
    const fspectrum = new Float32Array(data);
    
    //console.log("Sweep"+fspectrum[0]);
    if(this.oldsweep==fspectrum[0])
    {
        
        if(this.onsweep!=0)
            this.databin = new Uint16Array(fspectrum.length);
        this.onsweep=0;
    }
    else
    {
        
        
        if(this.onsweep!=1)
            this.databin = new Uint16Array(fspectrum.length*8);
        this.onsweep=1;
    }
    this.oldsweep=fspectrum[0];
       
    
    
    if (!this.paused) {
        for (let i = 0; i < fspectrum.length; i++) {
            // Convertir l'élément en entier non signé 16 bits et le stocker dans le tableau Uint16Array
            this.databin[i+fspectrum.length*this.oldsweep] = Math.log(fspectrum[i])*100;
        }
        if (this.databin.length != this.wf_size) {
            this.wf_size = this.databin.length;
            this.xoffset=0;//-this.wf_size /2;
            this.zoom=this.ctx.canvas.width/this.wf_size;
            this.zoom_mini=this.zoom;
            this.ctx_wf.canvas.width = this.databin.length;
            this.ctx_wf.fillStyle = "black";
            this.ctx_wf.fillRect(0, 0, this.wf.width, this.wf.height);
            this.imagedata = this.ctx_wf.createImageData(this.databin.length, 1);
        }
        if(this.onsweep==0)
        {
            this.drawSpectrum(this.databin);
            this.addWaterfallRow(this.databin);
        }
        else
        {
            if(this.oldsweep==7)
            {
                this.drawSpectrum(this.databin);
                this.addWaterfallRow(this.databin);
            }
        }    
        this.resize();
    }
}

Spectrum.prototype.updateSpectrumRatio = function() {
    this.spectrumHeight = Math.round(this.canvas.height * this.spectrumPercent / 100.0);

    this.gradient = this.ctx.createLinearGradient(0, 0, 0, this.spectrumHeight);
    for (var i = 0; i < this.colormap.length; i++) {
        var c = this.colormap[this.colormap.length - 1 - i];
        this.gradient.addColorStop(i / this.colormap.length,
            "rgba(" + c[0] + "," + c[1] + "," + c[2] + ", 1.0)");
    }
}

Spectrum.prototype.resize = function() {
    var width = this.canvas.clientWidth;
    var height = this.canvas.clientHeight;

    if (this.canvas.width != width ||
        this.canvas.height != height) {
        this.canvas.width = width;
        this.canvas.height = height;
        this.updateSpectrumRatio();
    }

    if (this.axes.width != width ||
        this.axes.height != this.spectrumHeight) {
        this.axes.width = width;
        this.axes.height = this.spectrumHeight;
        this.updateAxes();
    }

}

Spectrum.prototype.setSpectrumPercent = function(percent) {
    if (percent >= 0 && percent <= 100) {
        this.spectrumPercent = percent;
        this.updateSpectrumRatio();
    }
}

Spectrum.prototype.incrementSpectrumPercent = function() {
    if (this.spectrumPercent + this.spectrumPercentStep <= 100) {
        this.setSpectrumPercent(this.spectrumPercent + this.spectrumPercentStep);        
    }
}

Spectrum.prototype.decrementSpectrumPercent = function() {
    if (this.spectrumPercent - this.spectrumPercentStep >= 0) {
        this.setSpectrumPercent(this.spectrumPercent - this.spectrumPercentStep);
    }
}

Spectrum.prototype.toggleColor = function() {
    this.colorindex++;
    if (this.colorindex >= colormaps.length)
        this.colorindex = 0;
    this.colormap = colormaps[this.colorindex];
    this.updateSpectrumRatio();
}

Spectrum.prototype.setRange = function(min_db, max_db) {
    this.min_db = min_db;
    this.max_db = max_db;
    this.updateAxes();
}

Spectrum.prototype.rangeUp = function() {
    this.setRange(this.min_db - 5, this.max_db - 5);
}

Spectrum.prototype.rangeDown = function() {
    this.setRange(this.min_db + 5, this.max_db + 5);
}

Spectrum.prototype.rangeIncrease = function() {
    this.setRange(this.min_db - 5, this.max_db + 5);
}

Spectrum.prototype.rangeDecrease = function() {
    if (this.max_db - this.min_db > 10)
        this.setRange(this.min_db + 5, this.max_db - 5);
}

Spectrum.prototype.doAutoScale = function(bins) {
    var maxbinval = Math.max(...bins);
    var minbinval = Math.min(...bins);
          
    this.setRange(Math.ceil(minbinval * 0.075) * 10, Math.ceil(maxbinval * 0.075) * 10);  // 75% to nearest 10
    this.toggleAutoScale();      
}

Spectrum.prototype.setCenterHz = function(hz) {
    this.centerHz = hz;
    this.updateAxes();
}

Spectrum.prototype.setSpanHz = function(hz) {
    this.spanHz = hz;
    this.updateAxes();
}

Spectrum.prototype.setGain = function(gain) {
    this.gain = gain;
    this.updateAxes();
}

Spectrum.prototype.setFps = function(fps) {
    this.fps = fps;
    this.updateAxes();
}

Spectrum.prototype.setAveraging = function(num) {
    if (num >= 0) {
        this.averaging = num;
        this.alpha = 2 / (this.averaging + 1)
    }
}

Spectrum.prototype.setTuningStep = function(num) {
    if (num > 0 && num < 10e6) 
        this.tuningStep = num;
    this.log("Step: " + this.tuningStep);         
}

Spectrum.prototype.incrementAveraging = function() {
    this.setAveraging(this.averaging + 1);
}

Spectrum.prototype.decrementAveraging = function() {
    if (this.averaging > 0) {
        this.setAveraging(this.averaging - 1);
    }
}

Spectrum.prototype.incrementFrequency = function() { 
    var freq = { freq : this.centerHz + this.tuningStep };
    this.ws.send(JSON.stringify(freq));               
}

Spectrum.prototype.decrementFrequency = function() { 
    var freq = { freq : this.centerHz - this.tuningStep};       
    this.ws.send(JSON.stringify(freq));                
}

Spectrum.prototype.incrementGain = function() { 
    var gain = { gain : this.gain + 1 };
    this.ws.send(JSON.stringify(gain));               
}

Spectrum.prototype.decrementGain = function() { 
    var gain = { gain : this.gain - 1 };
    this.ws.send(JSON.stringify(gain));               
}

Spectrum.prototype.incrementFps = function() { 
    var fps = { fps : this.fps + 5 };
    this.ws.send(JSON.stringify(fps));               
}

Spectrum.prototype.decrementFps = function() { 
    var fps = { fps : this.fps - 5 };
    this.ws.send(JSON.stringify(fps));               
}

Spectrum.prototype.decrementTuningStep = function() {  // 1ex, 2.5ex, 5ex
    if (this.tuningStep > 1) {
        
        var step;
        var firstDigit = parseInt(this.tuningStep / Math.pow(10, parseInt(Math.log10(this.tuningStep))));
        
        if (firstDigit == 2) 
            step = 2.5;
        else 
            step = 2;
        
        this.setTuningStep(this.tuningStep / step);
    }
}

Spectrum.prototype.incrementTuningStep = function() {
    if (this.tuningStep > 0) {

        var step;
        var firstDigit = parseInt(this.tuningStep / Math.pow(10, parseInt(Math.log10(this.tuningStep))));
        
        if (firstDigit > 1) 
            step = 2;
        else 
            step = 2.5;
        
        this.setTuningStep(this.tuningStep * step);
        
    }
}

Spectrum.prototype.downloadWFImage = function(){
    var link = document.createElement('a');
    var dateString = new Date().toISOString().replace(/:/g,'-');
    link.download = 'capture-' + dateString + '.png';
    link.href = this.wf.toDataURL();
    link.click();
}

Spectrum.prototype.setPaused = function(paused) {
    this.paused = paused;
}

Spectrum.prototype.togglePaused = function() {
    this.setPaused(!this.paused);
}

Spectrum.prototype.setMaxHold = function(maxhold) {
    this.maxHold = maxhold;
    this.binsMax = undefined;
}

Spectrum.prototype.setAutoScale = function(autoscale) {
    this.autoScale = autoscale;    
}

Spectrum.prototype.toggleMaxHold = function() {
    this.setMaxHold(!this.maxHold);
}

Spectrum.prototype.toggleAutoScale = function() {
    this.setAutoScale(!this.autoScale);
}

Spectrum.prototype.log = function(message) {
    this.logger.innerHTML = message + '<br/>';
    this.logger.scrollTop = this.logger.scrollHeight; 
}

Spectrum.prototype.setWebSocket = function(ws) {
    this.ws = ws;
}

Spectrum.prototype.toggleFullscreen = function() {
    if (!this.fullscreen) {
        if (this.canvas.requestFullscreen) {
            this.canvas.requestFullscreen();
        } else if (this.canvas.mozRequestFullScreen) {
            this.canvas.mozRequestFullScreen();
        } else if (this.canvas.webkitRequestFullscreen) {
            this.canvas.webkitRequestFullscreen();
        } else if (this.canvas.msRequestFullscreen) {
            this.canvas.msRequestFullscreen();
        }
        this.fullscreen = true;
    } else {
        if (document.exitFullscreen) {
            document.exitFullscreen();
        } else if (document.mozCancelFullScreen) {
            document.mozCancelFullScreen();
        } else if (document.webkitExitFullscreen) {
            document.webkitExitFullscreen();
        } else if (document.msExitFullscreen) {
            document.msExitFullscreen();
        }
        this.fullscreen = false;
    }
}

Spectrum.prototype.zoomin= function()
{
    this.spanHz = (this.spanHz *this.zoom);
    this.zoom = this.zoom+1;
    //this.spanHz = (this.spanHz /this.zoom);
    this.setSpanHz((this.spanHz /this.zoom));

    //this.setCenterHz(this.centerHz-(this.spanHz /2));
}


Spectrum.prototype.zoomout= function()
{
    this.spanHz = (this.spanHz *this.zoom);
    this.zoom = this.zoom-1;
    if(this.zoom<1)
        this.zoom=1;
    this.setSpanHz((this.spanHz /this.zoom));
    
    //this.setCenterHz(this.centerHz-(this.spanHz /2));
    //this.spanHz = (this.spanHz /this.zoom);
    //this.resize();
    
}

Spectrum.prototype.scaleat = function(at, amount)
{
    
    if(this.zoom*amount>=this.zoom_mini)
    {    
        this.zoom *= amount;
        this.pos = at - (at - this.pos) * amount;
        //console.log("Original "+this.NativeSpan+" Span "+this.NativeSpan*(this.Screentobin(this.canvas.width)-this.Screentobin(0))/this.wf_size);    
       
       this.setSpanHz(this.NativeSpan*(this.Screentobin(this.canvas.width)-this.Screentobin(0))/this.wf_size);
        this.setCenterHz(((this.Screentobin(this.canvas.width/2)-this.wf_size/2)/this.wf_size*this.NativeSpan)+this.orginalfreq);
        //console.log("Freq "+((this.Screentobin(this.canvas.width/2)-this.wf_size/2)/this.wf_size*this.NativeSpan)+this.orginalfreq);
    }    
    
    
}

Spectrum.prototype.Screentobin= function(x)
{
    return (x-this.pos)/this.zoom+this.xoffset;
}

Spectrum.prototype.Bintoscreen= function(x)
{
    return ((x-this.xoffset)*this.zoom+this.pos);
}

Spectrum.prototype.UpdateXOffset= function(x)
{
    //console.log("screen "+this.Screentobin(this.canvas.width)+" bin "+this.Screentobin(0));
    if((this.xoffset+x)*this.zoom<this.pos)
        this.xoffset=this.pos/this.zoom;
    else
    if(this.Screentobin(this.canvas.width)>this.wf_size)
        {
            this.xoffset-=x;    
            //console.log("Limite "+this.Bintoscreen(this.pos)+" "+this.Bintoscreen(this.wf_size)); 
            //this.xoffset=this.Screentobin(this.pos/this.zoom);
        }
        //this.xoffset=this.pos+this.wf_size;
    else    
        this.xoffset+=x;
    
    this.scaleat(this.xoffset,1);
}


Spectrum.prototype.UpdateYOffset= function(y)
{
    
    this.yoffset+=y;
    
}

Spectrum.prototype.onKeypress = function(e) {

    switch (e.key) {        
        case " ": 
            this.togglePaused();
            break; 
        case "S":
            this.toggleFullscreen();
            break;
        case "c":
            this.toggleColor();
            break;
        case "ArrowUp":
            this.rangeUp();
            break;
        case "ArrowDown":
            this.rangeDown();
            break;
        case "ArrowLeft":
            this.rangeDecrease();
            break;
        case "ArrowRight":
            this.rangeIncrease();
            break;
        case "W":
            this.incrementSpectrumPercent();
            break;
        case "w":
            this.decrementSpectrumPercent();
            break;
        case "+":
            this.incrementAveraging();
            break;
        case "-":
            this.decrementAveraging();
            break;
        case "m":
            this.toggleMaxHold();
            break;
        case "a":
            this.toggleAutoScale();
            break;
        case "f":
            this.decrementFrequency();
            break;
        case "F":
            this.incrementFrequency();
            break;
        case "g":
            this.decrementGain();
            break;
        case "G":
            this.incrementGain();
            break;
        case "p":
            this.decrementFps();
            break;
        case "P":
            this.incrementFps();
            break;
        case "t":
            this.decrementTuningStep();
            break;
        case "T":
            this.incrementTuningStep();
            break;
        case "d":
            this.downloadWFImage();
            break;
        case "x":
            this.zoomin();
            break;    
    }
}

Spectrum.prototype.handleMouseWheel = function(e)
{

    
    
    if(e.deltaY<0)
        {
        //this.zoomin();
        //this.scaleat(e.offsetX,1.2);
        this.scaleat(e.offsetX,1.2);
        //this.xoffset=e.OffsetX;
        }
    if(e.deltaY>0)
    {
        
        //this.scaleat(e.offsetX,1/1.2);
        this.scaleat(e.offsetX,1/1.2);
        
        //this.zoomout();
    }
    this.updateInfo(e.clientX);
}

Spectrum.prototype.handleMouseUp = function(e)
{
    this.mouseisdown=0;
}

Spectrum.prototype.handleMouseDown = function(e)
{
    this.mouseisdown=1;
}

Spectrum.prototype.handleMouseMove = function(e)
{
    if(this.mouseisdown==1)
    {
        this.UpdateXOffset(-(e.movementX*1)/this.zoom);

        if(e.movementY<0)
            this.rangeUp();
        if(e.movementY>0)
            this.rangeDown();
        //this.UpdateYOffset(-(e.movementY*10)/this.zoom);
    }
    this.updateInfo(e.clientX);
}

function Spectrum(id, options) {
    // Handle options
    this.centerHz = (options && options.centerHz) ? options.centerHz : 258e6;
    this.orginalfreq = this.centerHz;
    this.spanHz = (options && options.spanHz) ? options.spanHz : 480e6;
    this.NativeSpan=this.spanHz;
    this.gain = (options && options.gain) ? options.gain : 0;
    this.fps = (options && options.fps) ? options.fps : 0;
    this.wf_size = (options && options.wf_size) ? options.wf_size : 0;
    this.wf_rows = (options && options.wf_rows) ? options.wf_rows : 2048;
    this.spectrumPercent = (options && options.spectrumPercent) ? options.spectrumPercent : 25;
    this.spectrumPercentStep = (options && options.spectrumPercentStep) ? options.spectrumPercentStep : 5;
    this.averaging = (options && options.averaging) ? options.averaging : 0;
    this.maxHold = (options && options.maxHold) ? options.maxHold : false;
    this.autoScale = (options && options.autoScale) ? options.autoScale : false;

    this.logger = (options && options.logger) ? document.getElementById(options.logger) : document.getElementById('log');
    
    // Setup state
    this.paused = false;
    this.fullscreen = false;
    this.min_db = 600;
    this.max_db = 2500;
    this.spectrumHeight = 0;
    this.tuningStep = 100000;
    this.maxbinval = 0;
    this.minbinval = 0;
    this.wfrowcount = 0;

    // Colors
    this.colorindex = 0;
    this.colormap = colormaps[2];

    // Create main canvas and adjust dimensions to match actual
    this.canvas = document.getElementById(id);
    this.canvas.height = this.canvas.clientHeight;
    this.canvas.width = this.canvas.clientWidth;
    this.ctx = this.canvas.getContext("2d");
    this.ctx.fillStyle = "black";
    this.ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);

    // Create offscreen canvas for axes
    this.axes = document.createElement("canvas");
    this.axes.height = 1; // Updated later
    this.axes.width = this.canvas.width;
    this.ctx_axes = this.axes.getContext("2d");

    // Create offscreen canvas for waterfall
    this.wf = document.createElement("canvas");
    this.wf.height = this.wf_rows;
    this.wf.width = this.wf_size;
    this.ctx_wf = this.wf.getContext("2d");

    // Create offscreen canvas for Frequency
    this.InfoFrequency = document.createElement("canvas");
    this.InfoFrequency.height = 20; // Updated later
    this.InfoFrequency.width = this.canvas.width;
    this.ctx_InfoFrequency = this.axes.getContext("2d");

    this.zoom=1;
    this.mouseisdown=0;
    this.xoffset=0;
    this.yoffset=0;

    // Trigger first render
    this.setAveraging(this.averaging);
    this.updateSpectrumRatio();
    this.resize();

    this.oldsweep =0;
    this.onsweep=2;
    //this.pos=this.wf_size/2;
    this.pos=0;
    //this.scaleat(this.canvas.width/2,this.canvas.width/this.wf_size );
}
