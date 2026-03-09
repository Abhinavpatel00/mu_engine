https://github.com/vercidium-patreon/meshing

https://www.youtube.com/watch?v=i6VVegoRuy0
Kind: captions
Language: en
 
have you ever wanted to try a new game
 
have you ever wanted to try a new game
but your computer isn't good enough to
 
but your computer isn't good enough to
run it I've spent 6 years optimizing
 
run it I've spent 6 years optimizing
games and recently developed a new kind
 
games and recently developed a new kind
of lighting that doesn't need a powerful
 
of lighting that doesn't need a powerful
graphics card by lighting I mean these
 
graphics card by lighting I mean these
beams of sunlight there are two ways
 
beams of sunlight there are two ways
that most games render them but they
 
that most games render them but they
each have their flaw I've developed a
 
each have their flaw I've developed a
third approach that looks identical to
 
third approach that looks identical to
the second but runs 70 times faster the
 
the second but runs 70 times faster the
first method creates these beams of
 
first method creates these beams of
light using the Skybox this works by
 
light using the Skybox this works by
rendering the Sky Box to two textures
 
rendering the Sky Box to two textures
normally the fragment Shader only
 
normally the fragment Shader only
outputs one color to the screen but we
 
outputs one color to the screen but we
also want to save the bright parts of
 
also want to save the bright parts of
the Sky Box to another texture this is
 
the Sky Box to another texture this is
done using this brightness cut off value
 
done using this brightness cut off value
it only lets colors through if they're
 
it only lets colors through if they're
brighter than the cut off now we can
 
brighter than the cut off now we can
blow this texture away from the Sun to
 
blow this texture away from the Sun to
create streaks of light then we can
 
create streaks of light then we can
overlay this on top of the main texture
 
overlay this on top of the main texture
this runs pretty quickly but as soon as
 
this runs pretty quickly but as soon as
you look away from the Sun the beams
 
you look away from the Sun the beams
disappear if you want them to stay on
 
disappear if you want them to stay on
screen you need to use the second
 
screen you need to use the second
approach when you're playing a 3D game
 
approach when you're playing a 3D game
everything is still rendered to a flat
 
everything is still rendered to a flat
2D surface that's because your monitor
 
2D surface that's because your monitor
is a flat 2D screen so how can we draw
 
is a flat 2D screen so how can we draw
3D beams of light over a 2D image when I
 
3D beams of light over a 2D image when I
was a kid I used to look at the TV from
 
was a kid I used to look at the TV from
the side to try and see what's around
 
the side to try and see what's around
the corner but sadly for little Mitch
 
the corner but sadly for little Mitch
that never worked but 20 years later
 
that never worked but 20 years later
I've discovered a way to look behind the
 
I've discovered a way to look behind the
scenes right now you're looking at every
 
scenes right now you're looking at every
pixel that's part of the final image and
 
pixel that's part of the final image and
you can see that they actually have a 3D
 
you can see that they actually have a 3D
position but you never know when look
 
position but you never know when look
looking from the front since these
 
looking from the front since these
pixels have 3D positions that means we
 
pixels have 3D positions that means we
can add 3D beams of light to this scene
 
can add 3D beams of light to this scene
to do this we'll create a line between
 
to do this we'll create a line between
each pixel and our camera then we'll
 
each pixel and our camera then we'll
test points along this line and if
 
test points along this line and if
they're in sunlight we'll draw a
 
they're in sunlight we'll draw a
transparent sun-colored pixel there when
 
transparent sun-colored pixel there when
we look at this from the front we can
 
we look at this from the front we can
see these streaks of light as the day
 
see these streaks of light as the day
passes the angle of these beams will
 
passes the angle of these beams will
match the position of the sun the only
 
match the position of the sun the only
problem is my graphics card is making a
 
problem is my graphics card is making a
weird noise and it's probably going to
 
weird noise and it's probably going to
overheat we can speed this up by
 
overheat we can speed this up by
choosing less points along each line but
 
choosing less points along each line but
that doesn't look as good so let's use a
 
that doesn't look as good so let's use a
third approach that renders 70 times
 
third approach that renders 70 times
quicker I learned this technique from
 
quicker I learned this technique from
the game among trees it places these 2D
 
the game among trees it places these 2D
planes in front of each window to create
 
planes in front of each window to create
the illusion of 3D light it works great
 
the illusion of 3D light it works great
until you know it's there if we try to
 
until you know it's there if we try to
recreate this we could render 2D planes
 
recreate this we could render 2D planes
on the edge of every window this might
 
on the edge of every window this might
look all right for a trailer video but
 
look all right for a trailer video but
the illusion is broken when playing the
 
the illusion is broken when playing the
game we can improve this system by r ing
 
game we can improve this system by r ing
these beams dynamically let's say we're
 
these beams dynamically let's say we're
in a clearing in a forest we'll choose
 
in a clearing in a forest we'll choose
random positions and draw Flat
 
random positions and draw Flat
rectangles there then we'll apply this
 
rectangles there then we'll apply this
sunc colored gradient to them and lastly
 
sunc colored gradient to them and lastly
we'll angle these planes towards the sun
 
we'll angle these planes towards the sun
if you don't look closely this probably
 
if you don't look closely this probably
looks all right but there's a few
 
looks all right but there's a few
problems this tree has a beam of light
 
problems this tree has a beam of light
cutting right through the middle of it
 
cutting right through the middle of it
and out here in the ocean these beams of
 
and out here in the ocean these beams of
light shouldn't be rendered for this
 
light shouldn't be rendered for this
approach to work we need to only render
 
approach to work we need to only render
beams of light at the edge of a shadow
 
beams of light at the edge of a shadow
to do this we'll take 16 samples around
 
to do this we'll take 16 samples around
each beam of light then the vertex Sher
 
each beam of light then the vertex Sher
will tally up how many of these samples
 
will tally up how many of these samples
are in sunlight the beam should be
 
are in sunlight the beam should be
brightest when half of them are in
 
brightest when half of them are in
sunlight it's easier to visualize this
 
sunlight it's easier to visualize this
with a graph when eight samples are in
 
with a graph when eight samples are in
sunlight the beam will have 100% opacity
 
sunlight the beam will have 100% opacity
then as the amount of samples decreases
 
then as the amount of samples decreases
or increases the opacity of the beam
 
or increases the opacity of the beam
decreases now we can hide the beams that
 
decreases now we can hide the beams that
are completely in Shadow or completely
 
are completely in Shadow or completely
in sunlight but when the trees start
 
in sunlight but when the trees start
swaying in the wind they flicker all
 
swaying in the wind they flicker all
over the place we can reduce the
 
over the place we can reduce the
flickering by taking more samples but
 
flickering by taking more samples but
that slows everything down a better
 
that slows everything down a better
solution would be to average the beam's
 
solution would be to average the beam's
opacity over time but shaders aren't
 
opacity over time but shaders aren't
built for measuring averages they
 
built for measuring averages they
convert data into triangles write pixels
 
convert data into triangles write pixels
to the screen then forget they ever
 
to the screen then forget they ever
existed luckily someone at the opengl
 
existed luckily someone at the opengl
team thought of this and came up with
 
team thought of this and came up with
vertex transform readback this lets us
 
vertex transform readback this lets us
save the data produced by the vertex
 
save the data produced by the vertex
shadar into another buffer in this case
 
shadar into another buffer in this case
the buffer stores the OPAC of every
 
the buffer stores the OPAC of every
vertex in every beam each beam is
 
vertex in every beam each beam is
composed of six vertices which is why
 
composed of six vertices which is why
each value is repeated six times now
 
each value is repeated six times now
this is where we use shaders in a way
 
this is where we use shaders in a way
that's not normal the opacity is sent to
 
that's not normal the opacity is sent to
both the fragment Shader and the
 
both the fragment Shader and the
transform readback buffer the CPU then
 
transform readback buffer the CPU then
reads this data and averages each beam's
 
reads this data and averages each beam's
opacity over a second the average values
 
opacity over a second the average values
are then stored in a Shader storage
 
are then stored in a Shader storage
buffer object this is a special buffer
 
buffer object this is a special buffer
that we can store any kind of data in
 
that we can store any kind of data in
the vertex Shader will then read the
 
the vertex Shader will then read the
averaged opacity from from this ssbo and
 
averaged opacity from from this ssbo and
pass it to the fragment Shader the
 
pass it to the fragment Shader the
reason this isn't a typical Shader setup
 
reason this isn't a typical Shader setup
is because the fragment Shader
 
is because the fragment Shader
completely ignores the opacity value
 
completely ignores the opacity value
instead it uses the average opacity to
 
instead it uses the average opacity to
render the beams in the vertex Shader
 
render the beams in the vertex Shader
we'll first Define our ssbo dat at the
 
we'll first Define our ssbo dat at the
top then we'll read the average opacity
 
top then we'll read the average opacity
from it and send it to the fragment
 
from it and send it to the fragment
Shader our beams of light will now fade
 
Shader our beams of light will now fade
in and out smoothly as the trees Sway in
 
in and out smoothly as the trees Sway in
the wind this renders 70 times faster
 
the wind this renders 70 times faster
than the second approach and looks the
 
than the second approach and looks the
same
same
 
same
you can run this demo and download the
 
you can run this demo and download the
code from the video description I
 
code from the video description I
optimized a different part of this
 
optimized a different part of this
engine up to 12,000 frames per second
 
engine up to 12,000 frames per second
which you can watch in this video on
 
which you can watch in this video on
screen
