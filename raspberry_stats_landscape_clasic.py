import datetime
import time
import board
import adafruit_ssd1306
import sys
from PIL import Image, ImageDraw, ImageFont
import os

from dataclasses import dataclass
import subprocess
assets_path = os.path.join(os.path.dirname(os.path.realpath(__file__)), "assets")


@dataclass
class Display:
    disp: adafruit_ssd1306
    image: Image
    draw: ImageDraw
    padding: int
    top: int
    bottom: int
    x: int
    # cache icons (use PIL Image class type for hints)
    icon_net: Image.Image | None = None
    icon_cpu: Image.Image | None = None
    icon_temp: Image.Image | None = None
    icon_mem: Image.Image | None = None
    icon_disk: Image.Image | None = None

@dataclass
class Config:
    RST: int
    DC: int
    SPI_PORT: int
    SPI_DEVICE: int
    FIRST_TIME: bool
    REFRESH_TIME: float
    FONT: ImageFont = ImageFont.load_default()
    SHUTDOWN: bool = False
    START_TIME: time=datetime.time(7, 00, 0)
    SHUTDOWN_TIME: time=datetime.time(23, 50, 0)

@dataclass
class SystemStats:
    IP: str
    CPU: str
    MEM: str
    DISK: str
    TEMP: str

def config() -> Config:
    is_shutdown = False
    if len(sys.argv) > 1:
        if sys.argv[1] == "shutdown":
            is_shutdown = True
    
    # custom font and size
    font_size = 12
    font = ImageFont.truetype("Hack-Regular.ttf", font_size)

    return Config(
        RST=None,
        DC=23,
        SPI_PORT=0,
        SPI_DEVICE=0,
        FIRST_TIME=True,
        REFRESH_TIME=0.3,
        SHUTDOWN=is_shutdown,
        FONT=font,
        START_TIME=datetime.time(7, 00, 0), 
        SHUTDOWN_TIME=datetime.time(23, 30, 0),
    )

def display(conf: Config) -> Display:
    i2c = board.I2C() 
    display = adafruit_ssd1306.SSD1306_I2C(128,32, i2c, addr=0x3C, reset=None)
    display.fill(0)
    display.show()

    # clear display

    # create blank image for drawing
    image = Image.new('1', (display.width, display.height))
    padding = -2

    draw = ImageDraw.Draw(image)

    d = Display(
        disp=display,
        image=image,
        # get drawing object to draw on image
        draw=draw,
        padding=padding,
        top=padding,
        bottom=display.height-padding,
        x=0,
    )
    # preload icons once
    try:
        d.icon_net = Image.open(assets_path+'/images/network.png').convert('1')
        d.icon_cpu = Image.open(assets_path+'/images/cpu.png').convert('1')
        d.icon_temp = Image.open(assets_path+'/images/temp.png').convert('1')
        d.icon_mem = Image.open(assets_path+'/images/mem.png').convert('1')
        d.icon_disk = Image.open(assets_path+'/images/storage.png').convert('1')
    except Exception:
        pass
    return d

def shutdown(msg: str, display: Display, conf: Config) -> None:
    # clear display
    display.disp.fill(0)
    display.disp.show()

    # create blank image for drawing
    display.draw.rectangle((0,0,display.disp.width,display.disp.height), outline=0, fill=0)

    # write message
    display.draw.text((display.x, display.top), msg, font=conf.FONT, fill=255,)

    # display image
    display.disp.image(display.image)
    display.disp.show()

    # wait 3 seconds
    time.sleep(3)
    bootImage(display)
    time.sleep(3)
    
    # clear display and exit
    display.disp.fill(0)
    display.disp.show()
    sys.exit()

def timeToWork(config: Config) -> None:
    # Obtener la hora actual
    ahora = datetime.datetime.now().time()

    # validate if ahora is between START_TIME and SHUTDOWN_TIME
    if config.START_TIME <= ahora < config.SHUTDOWN_TIME:
        return None
        # Aquí puedes agregar el código para continuar con el proceso
    else:
        shutdown("fuera de horario", display(config), config)

def bootImage(display: Display):
    # Draw a black filled box to clear the image.
    display.draw.rectangle((0,0,display.disp.width,display.disp.height), outline=0, fill=0)

    image = Image.open(assets_path+'/ppm/rasp_logo.ppm').convert('1')
    # Display image.
    display.disp.image(image)
    display.disp.show()
    time.sleep(3)

# Shell scripts for system monitoring from here : https://unix.stackexchange.com/questions/119126/command-to-display-memory-usage-disk-usage-and-cpu-load
def getSystemStats() -> SystemStats:
    cmd = "hostname -I | cut -d\' \' -f1"
    IP = subprocess.check_output(cmd, shell = True )
    cmd = "top -bn1 | grep load | awk '{printf \"CPU AVG: %.2f\", $(NF-2)}'"
    CPU = subprocess.check_output(cmd, shell = True )
    cmd = "free -m | awk 'NR==2{printf \"%.1f/%.1fGB %.1f%%\", $3/1024,$2/1024,$3*100/$2 }'"
    MEM = subprocess.check_output(cmd, shell = True )
    cmd = "df -h | awk '$NF==\"/\"{printf \"%d/%dGB %s\", $3,$2,$5}'"
    DISK = subprocess.check_output(cmd, shell = True )
    cmd = "vcgencmd measure_temp | awk -F= '{printf \"CPU Tmp: %s\", $2}'"
    TEMP = subprocess.check_output(cmd,shell=True)

    return SystemStats(
        IP=IP.decode(),
        CPU=CPU.decode(),
        MEM=MEM.decode(),
        DISK=DISK.decode(),
        TEMP=TEMP.decode()
    )        


count_for_disk=0
def main(conf: Config, disp: Display) -> None:
    global count_for_disk

    # time to work?
    timeToWork(conf)

    # Draw a black filled box to clear the image.
    disp.draw.rectangle((0,0,disp.disp.width,disp.disp.height), outline=0, fill=0)
    
    # get system stats
    systemStats = getSystemStats()

    if count_for_disk < 20:
        icon = disp.icon_net
        if icon:
            disp.draw.bitmap((disp.x, disp.top+1), icon, fill=255)
        disp.draw.text((disp.x+20, disp.top+1),       systemStats.IP,  font=conf.FONT, fill=25)

        # Write CPU stats.
        icon = disp.icon_cpu
        if icon:
            disp.draw.bitmap((disp.x, disp.top+19), icon, fill=255)
        disp.draw.text((disp.x+20, disp.top+20),     systemStats.CPU, font=conf.FONT, fill=25)
    
    # switch between stats
    if count_for_disk >= 20 and count_for_disk < 40:
        # write cpu temp
        icon = disp.icon_temp
        if icon:
            disp.draw.bitmap((disp.x, disp.top+1), icon, fill=255)
        disp.draw.text((disp.x+20, disp.top+1),       systemStats.TEMP,  font=conf.FONT, fill=25)

        # Write CPU stats.
        icon = disp.icon_cpu
        if icon:
            disp.draw.bitmap((disp.x, disp.top+19), icon, fill=255)
        disp.draw.text((disp.x+20, disp.top+20),     systemStats.CPU, font=conf.FONT, fill=25)
    
    # Write memory stats and disk stats.
    elif count_for_disk >=40 and count_for_disk <=60:
        icon = disp.icon_mem
        if icon:
            disp.draw.bitmap((disp.x, disp.top+1), icon, fill=255)
        disp.draw.text((disp.x+22, disp.top+1),    systemStats.MEM,  font=conf.FONT, fill=25)

        icon = disp.icon_disk
        if icon:
            disp.draw.bitmap((disp.x, disp.top+19), icon, fill=255)
        disp.draw.text((disp.x+22, disp.top+20),    systemStats.DISK,  font=conf.FONT, fill=25)
        if count_for_disk == 60:
            count_for_disk=0

    # Display image.
    disp.disp.image(disp.image)
    disp.disp.display()
    time.sleep(conf.REFRESH_TIME)
    count_for_disk=count_for_disk+1


if __name__ == "__main__":
    # shutdown?
    conf = config()
    _display = display(conf)
    if conf.SHUTDOWN:
        shutdown("Apagando...", _display, conf)
    
    # boot image
    bootImage(_display)

    while True:
        main(conf=conf, disp=_display)
