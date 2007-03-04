
import breve

class Mobile( breve.Real ):
	'''Mobile objects are objects in the simulated world which move around 		 and interact with other objects.  This is in contrast to  OBJECT(Stationary) objects which can collide and interact with  other objects but which never move. <P> When a Mobile object is created, it will be by default a simple  sphere.  You can change the appearence of this sphere by using methods in this class, or its parent class OBJECT(Real).  Or  you can change the shape altogether with the method METHOD(set-shape).'''

	__slots__ = [ 'archiveAcceleration', 'archiveLocation', 'archiveRotation', 'archiveRvelocity', 'archiveVelocity', 'linkForce', 'linkTorque', 'physicsEnabled', ]

	def __init__( self ):
		breve.Real.__init__( self )
		self.archiveAcceleration = breve.vector()
		self.archiveLocation = breve.vector()
		self.archiveRotation = breve.matrix()
		self.archiveRvelocity = breve.vector()
		self.archiveVelocity = breve.vector()
		self.linkForce = breve.vector()
		self.linkTorque = breve.vector()
		self.physicsEnabled = 0
		Mobile.init( self )

	def archive( self ):
		''''''


		self.archiveLocation = self.getLocation()
		self.archiveRotation = self.getRotation()
		self.archiveVelocity = self.getVelocity()
		self.archiveRvelocity = self.getRotationalVelocity()
		self.archiveAcceleration = self.getAcceleration()
		return breve.Real.archive( self,)


	def checkForPenetrations( self ):
		'''Depricated.'''


		return self.getCollidingObjects()


	def checkForSelfPenetrations( self ):
		'''Determines whether this link is currently penetrating with other links in the same multibody.  This is not meant as a general purpose collision detection tool -- it is meant to detect potentially troublesome configurations of links when they are created.'''


		return breve.breveInternalFunctionFinder.linkCheckSelfPenetration( self, self.realWorldPointer )


	def dearchive( self ):
		''''''


		self.realWorldPointer = breve.breveInternalFunctionFinder.linkNew( self)
		self.realWorldPointer = breve.breveInternalFunctionFinder.linkAddToWorld( self, self.realWorldPointer )
		self.setShape( self.objectShape )
		self.move( self.archiveLocation )
		self.setRotation( self.archiveRotation )
		self.setVelocity( self.archiveVelocity )
		self.setRotationalVelocity( self.archiveRvelocity )
		self.setAcceleration( self.archiveAcceleration )
		if self.physicsEnabled:
			self.enablePhysics()
		else:
			self.disablePhysics()

		return breve.Real.dearchive( self,)


	def disablePhysics( self ):
		'''Disables the physical simulation for a OBJECT(Mobile) object.'''


		self.physicsEnabled = 0
		breve.breveInternalFunctionFinder.linkSetPhysics( self, self.realWorldPointer, 0 )

	def enablePhysics( self ):
		'''Enables physical simulation for a OBJECT(Mobile) object.   This must be used in conjunction with a  OBJECT(PhysicalControl) object which sets up physical  simulation for the entire world.   <p> When physics is enabled for an object, the acceleration  can no longer be assigned manually--it will be computed  from the forces applied to the object.'''


		self.physicsEnabled = 1
		breve.breveInternalFunctionFinder.linkSetPhysics( self, self.realWorldPointer, 1 )

	def getAcceleration( self ):
		'''Returns the vector acceleration of this object.'''


		return breve.breveInternalFunctionFinder.linkGetAcceleration( self, self.realWorldPointer )


	def getBoundMaximum( self ):
		'''Returns the vector representing the maximum X, Y and Z locations of points on this link.'''


		return breve.breveInternalFunctionFinder.linkGetMax( self, self.realWorldPointer )


	def getBoundMinimum( self ):
		'''Returns the vector representing the minimum X, Y and Z locations of points on this link.'''


		return breve.breveInternalFunctionFinder.linkGetMin( self, self.realWorldPointer )


	def getCollidingObjects( self ):
		'''Returns a list of objects currently colliding with this object. This is not meant as a general purpose collision detection tool -- it is meant to detect potentially troublesome configurations of links when they are created.'''


		return breve.breveInternalFunctionFinder.linkGetPenetratingObjects( self, self.realWorldPointer )


	def getDistance( self, otherObject ):
		'''Returns the scalar distance from this object's center to  otherObject.'''


		return breve.length( ( self.getLocation() - otherObject.getLocation() ) )


	def getForce( self ):
		'''Returns the force acting on the object, which was previously  set using METHOD(set-force).  '''


		return self.linkForce


	def getLinkPointer( self ):
		'''For internal use only.'''


		return self.realWorldPointer


	def getMass( self ):
		'''Returns the mass of the object.'''


		return self.objectShape.getMass()


	def getRotationalVelocity( self ):
		'''Returns the vector angular velocity of this object.'''


		return breve.breveInternalFunctionFinder.linkGetRotationalVelocity( self, self.realWorldPointer )


	def getTorque( self ):
		'''Returns the torque acting on the object, which was previously  set using METHOD(set-torque).  '''


		return self.linkTorque


	def getVelocity( self ):
		'''Returns the vector velocity of this object.'''


		return breve.breveInternalFunctionFinder.linkGetVelocity( self, self.realWorldPointer )


	def init( self ):
		''''''


		self.e = 0.200000
		self.eT = 0.500000
		self.mu = 0.200000
		self.color = breve.vector( 1, 1, 1 )
		self.realWorldPointer = breve.breveInternalFunctionFinder.linkNew( self)
		breve.breveInternalFunctionFinder.linkAddToWorld( self, self.realWorldPointer )
		self.setShape( self.controller.getGenericShape() )

	def offset( self, amount ):
		'''Moves this object by amount, relative to its current position.'''


		self.move( ( self.getLocation() + amount ) )

	def point( self, theVertex, theLocation ):
		'''An easier way to rotate an object--this function rotates  an object such that the local point theVertex, points towards the world direction theLocation.  In other words, theLocation is where you want the object to face, and theVertex indicates  which side of the object is to be considered the "front".'''

		v = breve.vector()
		a = 0

		if ( ( breve.length( theVertex ) == 0.000000 ) or ( breve.length( theLocation ) == 0.000000 ) ):
			return


		v = breve.breveInternalFunctionFinder.cross( self, theVertex, theLocation )
		a = breve.breveInternalFunctionFinder.angle( self, theVertex, theLocation )
		if ( breve.length( v ) == 0.000000 ):
			self.rotate( theVertex, 0.010000 )
			return



		self.rotate( v, a )

	def register( self, theShape ):
		'''Deprecated.  Don't use.'''


		print '''warning: the method "register" of Mobile is deprecated, use the method "set-shape" instead'''
		self.setShape( theShape )

	def relativeRotate( self, thisAxis, amount ):
		'''Sets the rotation of this object around vector axis thisAxis  by scalar amount (in radians).  This is a rotation relative to the  current position.'''

		length = 0

		length = breve.length( thisAxis )
		breve.breveInternalFunctionFinder.linkRotateRelative( self, self.realWorldPointer, thisAxis, amount )

	def removeLabel( self ):
		'''Removes the label that would be drawn next to an object.'''


		breve.breveInternalFunctionFinder.linkRemoveLabel( self, self.realWorldPointer )

	def resumePhysics( self ):
		''''''


		breve.breveInternalFunctionFinder.linkSetPhysics( self, self.realWorldPointer, self.physicsEnabled )

	def rotate( self, thisAxis, amount ):
		'''Deprecated.  Renamed to METHOD(set-rotation).'''


		self.setRotation( thisAxis, amount )

	def set( self, theShape ):
		'''Deprecated.  Don't use.'''


		print '''warning: the method "set" of Mobile is deprecated, use the method "set-shape" instead'''
		self.setShape( theShape )

	def setAcceleration( self, newAcceleration ):
		'''Sets the acceleration of this object to newAcceleration. This method has no effect if physical simulation is turned  on for the object, in which case the physical simulation  engine computes acceleration.'''


		breve.breveInternalFunctionFinder.linkSetAcceleration( self, self.realWorldPointer, newAcceleration )

	def setForce( self, newForce ):
		'''Sets the velocity acting on the object to newForce.  This  force will remain in effect until it is disabled with a  new call to METHOD(set-force).'''


		if ( not self.realWorldPointer ):
			print '''set-force called with uninitialized Mobile object'''
			return



		self.linkForce = newForce
		breve.breveInternalFunctionFinder.linkSetForce( self, self.realWorldPointer, self.linkForce )

	def setLabel( self, theLabel ):
		'''Sets the label to be drawn along side the object.'''


		breve.breveInternalFunctionFinder.linkSetLabel( self, self.realWorldPointer, theLabel )

	def setRotationEulerAngles( self, angles ):
		'''Sets the rotation of this object to the Euler angles specified  by angles (in radians).'''

		m = breve.matrix()
		r22 = 0
		r21 = 0
		r20 = 0
		r12 = 0
		r11 = 0
		r10 = 0
		r02 = 0
		r01 = 0
		r00 = 0

		r00 = ( ( breve.breveInternalFunctionFinder.cos( self, angles.z ) * breve.breveInternalFunctionFinder.cos( self, angles.x ) ) - ( ( breve.breveInternalFunctionFinder.cos( self, angles.y ) * breve.breveInternalFunctionFinder.sin( self, angles.x ) ) * breve.breveInternalFunctionFinder.sin( self, angles.z ) ) )
		r01 = ( ( breve.breveInternalFunctionFinder.cos( self, angles.z ) * breve.breveInternalFunctionFinder.sin( self, angles.x ) ) + ( ( breve.breveInternalFunctionFinder.cos( self, angles.y ) * breve.breveInternalFunctionFinder.cos( self, angles.x ) ) * breve.breveInternalFunctionFinder.sin( self, angles.z ) ) )
		r02 = ( breve.breveInternalFunctionFinder.sin( self, angles.z ) * breve.breveInternalFunctionFinder.cos( self, angles.y ) )
		r10 = ( ( ( -breve.breveInternalFunctionFinder.sin( self, angles.z ) ) * breve.breveInternalFunctionFinder.cos( self, angles.x ) ) - ( ( breve.breveInternalFunctionFinder.cos( self, angles.y ) * breve.breveInternalFunctionFinder.sin( self, angles.x ) ) * breve.breveInternalFunctionFinder.cos( self, angles.z ) ) )
		r11 = ( ( ( -breve.breveInternalFunctionFinder.sin( self, angles.z ) ) * breve.breveInternalFunctionFinder.sin( self, angles.x ) ) + ( ( breve.breveInternalFunctionFinder.cos( self, angles.y ) * breve.breveInternalFunctionFinder.cos( self, angles.x ) ) * breve.breveInternalFunctionFinder.cos( self, angles.z ) ) )
		r12 = ( breve.breveInternalFunctionFinder.cos( self, angles.z ) * breve.breveInternalFunctionFinder.sin( self, angles.y ) )
		r20 = ( breve.breveInternalFunctionFinder.sin( self, angles.y ) * breve.breveInternalFunctionFinder.sin( self, angles.x ) )
		r21 = ( ( -breve.breveInternalFunctionFinder.sin( self, angles.y ) ) * breve.breveInternalFunctionFinder.cos( self, angles.x ) )
		r22 = breve.breveInternalFunctionFinder.cos( self, angles.y )
		m = breve.matrix(  r00, r01, r02, r10, r11, r12, r20, r21, r22 )
		self.setRotation( m )

	def setRotationalAcceleration( self, newAcceleration ):
		'''Sets the rotational acceleration of this object to  newAcceleration.  This method has no effect if physical  simulation is turned on for the object, in which case the  physical simulation engine computes acceleration.'''


		breve.breveInternalFunctionFinder.linkSetRotationalAcceleration( self, self.realWorldPointer, newAcceleration )

	def setRotationalVelocity( self, angularVelocity ):
		'''Sets the rotational velocity of this object to  angularVelocity.'''


		breve.breveInternalFunctionFinder.linkSetRotationalVelocity( self, self.realWorldPointer, angularVelocity )

	def setTorque( self, newTorque ):
		'''Sets the torque acting on the object to newTorque.  This  torque will remain in effect until it is disabled with a  new call to METHOD(set-torque).'''


		if ( not self.realWorldPointer ):
			print '''set-torque called with uninitialized Mobile object'''
			return



		self.linkTorque = newTorque
		breve.breveInternalFunctionFinder.linkSetTorque( self, self.realWorldPointer, self.linkTorque )

	def setVelocity( self, newVelocity ):
		'''Sets the velocity of this object to newVelocity.'''


		if ( not self.realWorldPointer ):
			print '''set-velocity called with uninitialized Mobile object'''
			return



		breve.breveInternalFunctionFinder.linkSetVelocity( self, self.realWorldPointer, newVelocity )

	def suspendPhysics( self ):
		''''''


		breve.breveInternalFunctionFinder.linkSetPhysics( self, self.realWorldPointer, 0 )

	def transform( self, theVector ):
		'''Transforms theVector in the world coordinate frame to a  vector in the frame of this object.  '''


		return breve.breveInternalFunctionFinder.vectorFromLinkPerspective( self, self.realWorldPointer, theVector )



breve.Mobile = Mobile
breve.Mobiles = Mobile


