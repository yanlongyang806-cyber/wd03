
#if 0


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

typedef struct TestObject {
	Model*				model;
	Vec3				pos;
	WorldCollObject*	wco;
	F32					scale;
} TestObject;

TestObject** testObjects;

static void testObjectCollObjectMsgHandler(const WorldCollObjectMsg* msg){
	if(verify(msg->userType == WG_WCO_TEST_OBJECT)){
		TestObject* t = msg->userPointer;
		
		switch(msg->msgType){
			xcase WCO_MSG_GET_DEBUG_STRING:{
				sprintf_s(	msg->in.getDebugString.buffer,
							msg->in.getDebugString.bufferLen,
							"TestObject");
			}
			
			xcase WCO_MSG_DESTROYED:{
			}
			
			xcase WCO_MSG_GET_SHAPE:{
				WorldCollObjectMsgGetShapeOut*		getShape = msg->out.getShape;
				WorldCollObjectMsgGetShapeOutInst*	shapeInst;
				Model*								model = SAFE_MEMBER(t, model);
				ModelLOD*							model_lod = model?modelLoadLOD(model, 0):NULL;
				
				if(!model_lod){
					return;
				}
				
				printf(	"Creating test object at (%f, %f, %f) [%x, %x, %x]\n",
						vecParamsXYZ(t->pos),
						vecParamsXYZ((S32*)t->pos));

				copyMat3(unitmat, getShape->mat);
				copyVec3(t->pos, getShape->mat[3]);

				getShape->bodyDesc->scale = 1.0f;
				getShape->useBodyDesc = 1;

				wcoAddShapeInstance(getShape, &shapeInst);
				shapeInst->mesh = geoCookConvexMesh(model, NULL, 0);
				shapeInst->shapeGroupBit = WC_SHAPEGROUP_BIT_WORLD;
			}
			
			xcase WCO_MSG_BG_UPDATE_ACTOR:{
				if(rand() % 1000 < 10){
					PSDKActor*	actor = msg->in.bgUpdateActor.actor;
					Vec3		vel = {	100.f * qfrand(),
										100.f * qfrand(),
										100.f * qfrand()};
					
					psdkActorSetVel(actor, vel);
				}

				if(rand() % 1000 < 10){
					PSDKActor*	actor = msg->in.bgUpdateActor.actor;
					Vec3		vel = {	PI * qfrand(),
										PI * qfrand(),
										PI * qfrand()};
					
					psdkActorSetAngVel(actor, vel);
				}
			}
		}
	}
}

static void testKineObjectCollObjectMsgHandler(const WorldCollObjectMsg* msg){
	if(verify(msg->userType == WG_WCO_TEST_KINEMATIC)){
		TestObject* t = msg->userPointer;

		switch(msg->msgType){
			xcase WCO_MSG_GET_DEBUG_STRING:{
				sprintf_s(	msg->in.getDebugString.buffer,
							msg->in.getDebugString.bufferLen,
							"KinematicTestObject");
			}
			
			xcase WCO_MSG_DESTROYED:{
			}

			xcase WCO_MSG_GET_SHAPE:{
				WorldCollObjectMsgGetShapeOut*	getShape = msg->out.getShape;
				WorldCollObjectMsgGetShapeOutInst* shapeInst;
				Model*							model = SAFE_MEMBER(t, model);
				ModelLOD*						model_lod = model?modelLoadLOD(model, 0):NULL;

				if(!model_lod){
					return;
				}

				copyMat3(unitmat, getShape->mat);
				copyVec3(t->pos, getShape->mat[3]);

				getShape->bodyDesc->scale = 1.0f;
				getShape->useBodyDesc = 1;

				wcoAddShapeInstance(getShape, &shapeInst);
				shapeInst->mesh = geoCookConvexMesh(model, NULL, 0);
				shapeInst->shapeGroupBit = WC_SHAPEGROUP_BIT_WORLD;
			}
		}
	}
}

void wcCreateTestObject(const char* modelName,
						const Vec3 pos,
						F32 scale)
{
	Model* model = groupModelFind(modelName, 0);
	
	scale = 1.0f;
	
	if(	!model &&
		!stricmp(modelName, "box"))
	{
		model = groupModelFind("guide_box_10x10x10", 0);
	}
	
	if(model){
		Vec3 min, max;
		TestObject* t = callocStruct(TestObject);
		
		if(scale <= 0.0f){
			scale = 1.0f;
		}

		t->model = model;
		t->scale = scale;

		modelLODWaitForLoad(t->model, 0);

		copyVec3(pos, t->pos);
		
		eaPush(&testObjects, t);
		
		worldSetCollObjectMsgHandler(WG_WCO_TEST_OBJECT, testObjectCollObjectMsgHandler);

		addVec3(model->min, pos, min);
		addVec3(model->max, pos, max);

		wcoCreate(	&t->wco,
					worldGetActiveCollRoamingCell(),
					NULL,
					t,
					WG_WCO_TEST_OBJECT,
					min,
					max,
					0,
					0);
	}
}

void wcCreateTestKinematic(	int iPartitionIdx,
							const char* modelName,
							const Vec3 pos)
{
	Model* model = groupModelFind(modelName, 0);

	if(	!model &&
		!stricmp(modelName, "box"))
	{
		model = groupModelFind("guide_box_10x10x10", 0);
	}

	if(model){
		Vec3 min, max;
		TestObject* t = callocStruct(TestObject);

		t->model = model;
		t->scale = 1.0f;

		modelLODWaitForLoad(t->model, 0);

		copyVec3(pos, t->pos);

		eaPush(&testObjects, t);

		worldSetCollObjectMsgHandler(WG_WCO_TEST_KINEMATIC, testKineObjectCollObjectMsgHandler);

		addVec3(model->min, pos, min);
		addVec3(model->max, pos, max);

		wcoCreate(	&t->wco,
					NULL,
					worldGetActiveColl(iPartitionIdx),
					t,
					WG_WCO_TEST_KINEMATIC,
					min,
					max,
					1,
					0);
	}
}

void wcDestroyAllTestObjects(void){
	S32 i;
	
	for(i = 0; i < eaSize(&testObjects); i++){
		TestObject* t = testObjects[i];
		
		wcoDestroy(t->wco);
		t->wco = NULL;
		
		SAFE_FREE(testObjects[i]);
	}
	
	eaDestroy(&testObjects);
}

void wcDrawTestObjects(void){
	S32 i;
	
	for(i = 0; i < eaSize(&testObjects); i++){
		TestObject* t = testObjects[i];
		Model*		model = t->model;
		Mat4		mat;
		
		wcoGetMat(t->wco, mat);
		
		scaleMat3(mat, mat, t->scale);
		
		wlDrawModel(model, mat);
		
		if(0){
			Vec3 pos;
			S32 j;
			
			for(j = 0; j < 3; j++){
				copyVec3(mat[3], pos);
				scaleAddVec3(mat[j], 100, pos, pos);
				wlDrawLine3D_2(mat[3], 0xffffffff, pos, 0xff000000 | (0xff << (j * 8)));
			}
		}
	}
}

#endif
