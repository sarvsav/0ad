#include "timer.h"
#include "CommandManager.h"
#include "ObjectEntry.h"
#include "Unit.h"
#include "PaintObjectTool.h"
#include "MiniPatch.h"
#include "Terrain.h"
#include "Renderer.h"
#include "ObjectManager.h"
#include "PaintObjectCommand.h"

// rotate object at 180 degrees each second when applying to terrain
#define ROTATION_SPEED PI

// default tool instance
CPaintObjectTool CPaintObjectTool::m_PaintObjectTool;

CPaintObjectTool::CPaintObjectTool() : m_PaintCmd(0), m_Rotation(0)
{
	m_BrushSize=0;
}

void CPaintObjectTool::BuildTransform()
{
	m_ObjectTransform.SetIdentity();	
	m_ObjectTransform.RotateY(m_Rotation);
	m_ObjectTransform.Translate(m_Position);
}

void CPaintObjectTool::PaintSelection()
{
	if (m_PaintCmd) {
		// already applied object to terrain, now we're just rotating it
		double curtime=get_time();
		m_Rotation+=ROTATION_SPEED*float(curtime-m_LastTriggerTime);
		BuildTransform();
		m_PaintCmd->GetUnit()->GetModel()->SetTransform(m_ObjectTransform);
		m_LastTriggerTime=curtime;
	} else {
		m_Rotation=0;
		m_Position=m_SelectionPoint;
		m_LastTriggerTime=get_time();

		CObjectEntry* obj=g_ObjMan.GetSelectedObject();
		if (obj && obj->m_Model) {
			// get up to date transform 
			BuildTransform();

			// now paint the object
			m_PaintCmd=new CPaintObjectCommand(obj,m_ObjectTransform);
			m_PaintCmd->Execute();
			g_CmdMan.Append(m_PaintCmd);
		}
	}
}

void CPaintObjectTool::OnLButtonUp(unsigned int flags,int px,int py)
{
	CBrushTool::OnLButtonUp(flags,px,py);
	// terminate current command, if we've got one
	if (m_PaintCmd) {
		m_PaintCmd->Finalize();
		m_PaintCmd=0;
		m_Rotation=0;
	}
}

void CPaintObjectTool::OnDraw()
{
	// don't draw object if we're currently rotating it on the terrain
	if (m_PaintCmd) return;

	// don't draw unless we have a valid object to apply
	CObjectEntry* obj=g_ObjMan.GetSelectedObject();
	if (!obj || !obj->m_Model) return;

	// try to get object transform, in world space
	m_Position=m_SelectionPoint;
	BuildTransform();

	// render the current object at the same position as the selected tile
	m_Model=obj->m_Model;
	m_Model->SetTransform(m_ObjectTransform);
	g_Renderer.Submit(m_Model);
}
