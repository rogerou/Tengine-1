/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * License); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * AS IS BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*
 * Copyright (c) 2017, Open AI Lab
 * Author: haitao@openailab.com
 */
#include <iostream>
#include <vector>
#include <string>
#include <queue>
#include <set>

#include "static_graph.hpp"
#include "graph.hpp"



namespace TEngine {

void Graph::DumpGraph(void)
{

    std::cout<<"Graph: "<<name_<<" total nodes: "<<seq_nodes.size()<<std::endl;

    for(unsigned int i=0;i<seq_nodes.size();i++)
    {
        Node * p_node=seq_nodes[i];
        p_node->DumpNode();
    }

    std::cout<<"Input Nodes: "<<input_nodes.size()<<std::endl;
    for(unsigned i=0;i<input_nodes.size();i++)
    {
          std::cout<<"\t"<<i<<": "<<input_nodes[i]->GetName()<<std::endl;
    }
 

    std::cout<<"Output Nodes: "<<output_nodes.size()<<std::endl;
    for(unsigned i=0;i<output_nodes.size();i++)
    {
          std::cout<<"\t"<<i<<": "<<output_nodes[i]->GetName()<<std::endl;
    }
 
}


Node * Graph::FindNode(const std::string& node_name)
{
    int len=node_name.size();

    for(unsigned int i=0;i<seq_nodes.size();i++)
    {
          Node * node=seq_nodes[i];
          const std::string& target_name=node->GetName();

          int start=target_name.size()-len;

          if(start) 
               continue;

          if(target_name==node_name)
               return node;

    }

    return nullptr;
}


bool Graph::AddInputNode(const std::string& node_name)
{
    Node * node=FindNode(node_name);

    if(node==nullptr)
        return false;

    //check to avoid replicate node

    for(unsigned int i=0;i<input_nodes.size();i++)
    {
         if(node==input_nodes[i])
             return true;
    }

    input_nodes.push_back(node);

    return true;
}

bool Graph::AddOutputNode(const std::string& node_name)
{
    Node * node=FindNode(node_name);

    if(node==nullptr)
        return false;

    //check to avoid replicate node

    for(unsigned int i=0;i<output_nodes.size();i++)
    {
         if(node==output_nodes[i])
             return true;
    }

    output_nodes.push_back(node);

    return true;
}

Tensor * Graph::FindTensor(const std::string& tensor_name)
{
     if(tensor_map_.count(tensor_name))
         return tensor_map_[tensor_name];

    return nullptr;
}


bool Graph::CreateNodeFromStatic(Node * node, const StaticGraph * static_graph, const StaticNode * static_node)
{

      StaticOp* static_op=static_node->op.get();

      Operator * op=OpManager::CreateOp(static_op->name);

      if(op==nullptr)
      {
          XLOG_ERROR()<<"failed to create operator: "<<static_op->name<<"\n";
          return false;
      }


      /* TODO: copy attrs set in static_op */
      op->ParamFromStaticOp(static_op);

      node->SetOp(op);

      /* create output tensors */

      for(unsigned int i=0;i<static_node->output_tensor_list.size();i++)
      {
            int idx=static_node->output_tensor_list[i];
            StaticTensor * static_tensor=static_graph->tensor_list[idx].get();

            Tensor * tensor=new Tensor(static_tensor->name);

             tensor->SetDataType(static_tensor->data_type);
             tensor->SetType((TensorType)static_tensor->type);
         
             TShape& shape=tensor->GetShape();

             shape.SetDataLayout(static_tensor->data_layout);
             shape.SetDim(static_tensor->dims);

             if(static_tensor->type==kConstTensor)
             {
                 StaticConstTensor * const_tensor=dynamic_cast<StaticConstTensor *>(static_tensor);

                 (*tensor)["mem_addr"]=const_tensor->mem_addr;
                 (*tensor)["file_offset"]=const_tensor->file_offset;
                 (*tensor)["file_size"]=const_tensor->file_size;
             }

             tensor_map_[tensor->GetName()]=tensor;

             SetTensorOwner(tensor);

             node->SetOutputPort(i,tensor);

             const NodePort * port=node->GetOutputPort(i);

             tensor->producer=(NodePort *)port;
      }


      /* add node into list */
      node->SetNodeIndex(seq_nodes.size());
      seq_nodes.push_back(node);

      /* my node!*/
      SetNodeOwner(node);

      return true;
    
}

bool Graph::SetupConnection(Tensor * tensor, const StaticGraph * static_graph, const StaticTensor* static_tensor)
{
     /*will setup the tensor consumer and node inputs*/
     for(unsigned int i=0;i<static_tensor->consumer.size();i++)
     {
         const NodeSynapse * p_synapse=&static_tensor->consumer[i];
         const StaticNode * static_node=static_graph->node_list[p_synapse->node_index].get();

         Node * node=FindNode(static_node->name);
    
         /* create input port*/
         node->SetInputPort(p_synapse->entry_index, tensor); 

         const NodePort * port=node->GetInputPort(p_synapse->entry_index);

         tensor->AddConsumer((NodePort *)port);

     }

     return true;
}


bool Graph::RealCreateFromStatic(const StaticGraphPtr& static_graph)
{

    orig_graph_=static_graph;
    attrs_=static_graph->attrs;

    int node_number=static_graph->node_list.size();

    /* create node and its output tensor */
    for(int i=0;i<node_number;i++)
     {
           const StaticNode * node_ptr=static_graph->node_list[i].get();

           Node * node=new Node(node_ptr->name);

           if(!CreateNodeFromStatic(node,static_graph.get(),node_ptr))
               return false;
     }

     /* Setup the connections */
    int tensor_number=static_graph->tensor_list.size();

    for(int i=0;i<tensor_number;i++)
    {
        const StaticTensor * static_tensor=static_graph->tensor_list[i].get();
        Tensor * tensor=FindTensor(static_tensor->name);

        if(tensor==nullptr)
        {
            XLOG_ERROR()<<"cannot find tensor: "<<static_tensor->name<<"\n";
            return false;
        }

        if(!SetupConnection(tensor,static_graph.get(), static_tensor))
            return false;
    }
     
    /* set the input and output */

   for(unsigned int i=0;i<static_graph->input_node_list.size();i++)
   {
          int node_idx=static_graph->input_node_list[i];
          Node * node=FindNode(static_graph->node_list[node_idx].get()->name);
          input_nodes.push_back(node);
   }

   for(unsigned int i=0;i<static_graph->output_node_list.size();i++)
   {
          int node_idx=static_graph->output_node_list[i];
          Node * node=FindNode(static_graph->node_list[node_idx].get()->name);
          output_nodes.push_back(node);
   }

    /* re-order the seq_nodes_, removing un-used node, according to node dependency*/

    SanitizeGraph();

    return true;
}

Graph * Graph::CreateFromStatic(const StaticGraphPtr& static_graph)
{
    Graph * new_graph= new Graph(static_graph->model_name);

    if(new_graph->RealCreateFromStatic(static_graph))
         return new_graph;

    delete new_graph;

    return nullptr;
}     

bool Graph::RemoveTensor(Tensor * tensor)
{
   /* remove all refers from consumer */

   for(unsigned int i=0;i<tensor->consumer.size();i++)
   {
       NodePort * port=tensor->consumer[i];
       Node * node=port->owner;
       int  idx=port->port_index;

       node->RemoveInputPort(idx);
   }

   /* remove refer from producer */

   NodePort * port=tensor->producer;
   Node * node=port->owner;
   int  idx=port->port_index;

   node->RemoveOutputPort(idx);
   

   /* release the tensor*/

   auto ir=tensor_map_.find(tensor->GetName());

   if(ir!=tensor_map_.end())
       tensor_map_.erase(ir);

   /* if it is my tensor */
   if(RemoveTensorOwner(tensor))
      delete tensor;

   return true;  
}

bool Graph::RemoveNode(Node * node)
{

   std::vector<Tensor *> tensor_list;

   /* first, get all output tensors */
   for(unsigned int i=0;i<node->GetOutputNum();i++)
   {
      NodePort * port=node->GetOutputPortSeq(i);
      tensor_list.push_back(port->tensor);
   }

    /* removing all output tensor*/

   for(unsigned int i=0;i<tensor_list.size();i++)
   {
      Tensor *  tensor=tensor_list[i];

      RemoveTensor(tensor); 
   }

   /*remove the input refers for this node */

   std::vector<NodePort *> port_list;

   for(unsigned int n=0;n<node->GetInputNum();n++)
   {
        NodePort * port=node->GetInputPortSeq(n);
        port_list.push_back(port);
       
   }
 
   for(unsigned int n=0;n<port_list.size();n++)
   {
        NodePort * port=port_list[n];
        Tensor * tensor=port->tensor;

        tensor->RemoveConsumer(port);
   }

   /* remove from seq_nodes_ */
   auto ir=seq_nodes.begin();

   while(ir!=seq_nodes.end())
   {
        if((*ir)==node)
           break;
        ir++;
   }

   if(ir!=seq_nodes.end())  
       seq_nodes.erase(ir);

   /* remove from inputs/outputs */

   ir=input_nodes.begin();

   while(ir!=input_nodes.end())
   {
        if((*ir)==node)
        {
           input_nodes.erase(ir);
           break;
        }

        ir++;
   }


   ir=output_nodes.begin();

   while(ir!=output_nodes.end())
   {
        if((*ir)==node)
        {
           output_nodes.erase(ir);
           break;
        }

        ir++;
   }


   /* if it is my node, free it */

   if(RemoveNodeOwner(node))
         delete node;

   return true;
}


void Graph::SanitizeGraph(void)
{
    int node_number=seq_nodes.size();

    std::vector<Node *> new_seq;
    std::vector<int> access_flag(node_number,0);

    /* make sure the node index is correct first */
   for(int i=0;i<node_number;i++)
       seq_nodes[i]->SetNodeIndex(i);


    BFSVisit(this,output_nodes,graph_visit_t([&](Graph * graph, Node * node) {
            new_seq.insert(new_seq.begin(),node);
            access_flag[node->GetNodeIndex()]=1; 
    }));

    auto ir=seq_nodes.begin();

    for(unsigned int i=0;i<input_nodes.size();i++)
    {
         int input_index=input_nodes[i]->GetNodeIndex();

         if(!access_flag[input_index])
         {
             access_flag[input_index]=1;
             new_seq.insert(new_seq.begin(),input_nodes[i]);

         }
    }

    //removing node that can not be visited
    for(int i=0;i<node_number;i++)
    {
        if(access_flag[i])
        {
           ir++;
           continue;
        }

         Node * node=(*ir);

        

         ir=seq_nodes.erase(ir);

         if(!RemoveNode(node))
             break;
    }

    seq_nodes=new_seq;

    for(unsigned int i=0;i<seq_nodes.size();i++)
    {
         Node * node=seq_nodes[i];

        node->SetNodeIndex(i);
    }


    RemoveNoChildTensor();
}

bool Graph::IsOutputNode(Node * node)
{
   for(unsigned int i=0;i<output_nodes.size();i++)
   {
       if(output_nodes[i]==node)
             return true;
   }

   return false;
}

bool Graph::IsInputNode(Node * node)
{
   for(unsigned int i=0;i<output_nodes.size();i++)
   {
       if(input_nodes[i]==node)
             return true;
   }

   return false;
}

void Graph::RemoveNoChildTensor(void)
{  

   std::vector<Tensor *> tensor_list;

   auto tensor_ir=tensor_map_.begin();

    while(tensor_ir!=tensor_map_.end())
    {
         Tensor * tensor=tensor_ir->second;

          tensor_ir++;
          if(!tensor->consumer.size())
          {
              tensor_list.push_back(tensor);
          }
    }

    for(unsigned int i=0;i<tensor_list.size();i++)
    {
        Tensor * tensor=tensor_list[i];

        if(!IsOutputNode(tensor->producer->owner))
             RemoveTensor(tensor);
    }
}


static bool AllChildVisited(Graph * graph, Node * node, std::vector<int>& visited)
{
      for(unsigned int i=0;i<node->GetOutputNum();i++)
      {
           NodePort * port=node->GetOutputPort(i);
           Tensor *   tensor=port->tensor;

           for(unsigned int k=0;k<tensor->consumer.size();k++)
           {
                NodePort * in_port=tensor->consumer[k];
                Node * child=in_port->owner;
               if(!visited[child->GetNodeIndex()])
                   return false;
          }
      }
      return true;
}

static bool AllInputVisited(Graph * graph, Node * node, std::vector<int>& visited)
{
      for(unsigned int i=0;i<node->GetInputNum();i++)
      {
           NodePort * port=node->GetInputPort(i);
           Tensor *   tensor=port->tensor;
           NodePort * out_port=tensor->producer;
           Node * parent=out_port->owner;

            if(!visited[parent->GetNodeIndex()])
                   return false;
      }
      return true;
}

void Graph::BFSVisit(Graph * graph, std::vector<Node *>& starts,Graph::graph_visit_t func, bool backward)
{
    if(backward)
          BackwardBFS(graph,starts,func);
    else
          ForwardBFS(graph,starts,func);
}

void Graph::ForwardBFS(Graph * graph, std::vector<Node *>& starts, graph_visit_t func)
{
     int node_number=graph->seq_nodes.size();
     std::vector<int> visited(node_number,0);
     std::set<Node *> in_graph;

     for(int i=0;i<node_number;i++)
      in_graph.insert(graph->seq_nodes[i]);

     std::queue<Node *> visit_queue;

     /* inital the visit list */
     for(unsigned int i=0;i<starts.size();i++)
     {
         Node * node=starts[i];
         visit_queue.push(node);
         visited[node->GetNodeIndex()]=1;
         func(graph,node);
     }

     while(visit_queue.size())
     {
         Node * node=visit_queue.front();
         visit_queue.pop();

         int output_num=node->GetOutputNum();

         for(int i=0;i<output_num;i++)
         {
             NodePort * port=node->GetOutputPortSeq(i);
             Tensor * tensor=port->tensor;

             for(unsigned int k=0;k<tensor->consumer.size();k++)
             { 
                  Node * child=tensor->consumer[k]->owner;

                  if(in_graph.count(child) &&
                     !visited[child->GetNodeIndex()] 
                       && AllInputVisited(graph,child,visited))
                  {
                      visit_queue.push(child);
                      visited[child->GetNodeIndex()]=1;
                      func(graph,node);
                  }
             }
             
         }

         
     }

    
}


void Graph::BackwardBFS(Graph * graph, std::vector<Node *>& starts, graph_visit_t func )
{
     int node_number=graph->seq_nodes.size();
     std::vector<int> visited(node_number,0);
     std::queue<Node *> visit_queue;

     std::set<Node *> in_graph;

     for(int i=0;i<node_number;i++)
      in_graph.insert(graph->seq_nodes[i]);


     /* inital the visit list */
     for(unsigned int i=0;i<starts.size();i++)
     {
         Node * node=starts[i];
         visit_queue.push(node);
         visited[node->GetNodeIndex()]=1;
         func(graph,node); 
     }

     while(visit_queue.size())
     {
         Node * node=visit_queue.front();
         visit_queue.pop();


         int input_num=node->GetInputNum();

         for(int i=0;i<input_num;i++)
         {
             NodePort * port=node->GetInputPort(i);
             Tensor * tensor=port->tensor;
             Node * parent=tensor->producer->owner;

             if(in_graph.count(parent)  &&
                 !visited[parent->GetNodeIndex()] 
                 && AllChildVisited(graph,parent,visited))
             {
                  visit_queue.push(parent);
                  visited[parent->GetNodeIndex()]=1;
                  func(graph,parent); 
             }
         }
      }
         
}

void Graph::SetNodeOwner(Node * node)
{
    owned_nodes_.push_back(node);
}

void Graph::SetTensorOwner(Tensor * tensor)
{
   owned_tensors_[tensor->GetName()]=tensor;
}

bool Graph::RemoveNodeOwner(Node * node)
{
   auto ir=owned_nodes_.begin();

   while(ir!=owned_nodes_.end())
   {
       if((*ir)==node)
       {
           owned_nodes_.erase(ir);
           return true;
       }

       ir++;
   }

   return false;
}

bool Graph::RemoveTensorOwner(Tensor * tensor)
{
   auto ir=owned_tensors_.find(tensor->GetName());

   if(ir!=owned_tensors_.end())
   {
      owned_tensors_.erase(ir);
      return true;
   }

   return false;
}


Tensor * Graph::GetInputTensor(const std::string& name)
{
    for(unsigned int i=0;i<input_nodes.size();i++)
    {
        Node  * node=input_nodes[i];

       for(unsigned int j=0;j<node->GetInputNum();j++)
       {
          Tensor * tensor=node->GetInputTensor(j);

          if(tensor->GetName()==name)
             return tensor;
       }
    }
   
    return nullptr;
}

Tensor * Graph::GetOutputTensor(const std::string& name)
{
    for(unsigned int i=0;i<output_nodes.size();i++)
    {
        Node  * node=output_nodes[i];

        for(unsigned int j=0;j<node->GetOutputNum();j++)
        {
           Tensor * tensor=node->GetOutputTensor(j);

           if(tensor->GetName()==name)
              return tensor;
        }
    }
   
    return nullptr;
}


bool Graph::Replace(Subgraph * orig_sub, Subgraph * new_sub)
{
    //check if all input tensors are consumed  
    std::vector<Node *>& orig_input=orig_sub->input_nodes;

    for(unsigned int i=0;i<orig_input.size();i++)
    {
        Node * input_node=orig_input[i];

        for(unsigned int j=0;j<input_node->GetInputNum();j++)
        {
            const Tensor * tensor=input_node->GetInputTensor(j);

            if(tensor->GetType()==kConstTensor)
                 continue;

            if(new_sub->GetInputTensor(tensor->GetName()) == nullptr)
                     return false;
        }
    }

    //check if all output tensors are produced
    std::vector<Node *>& orig_output=orig_sub->output_nodes;

    for(unsigned int i=0;i<orig_output.size();i++)
    {
        Node * output_node=orig_output[i];

        for(unsigned int j=0;j<output_node->GetOutputNum();j++)
        {
            const Tensor * tensor=output_node->GetOutputTensor(j);
            if(new_sub->GetOutputTensor(tensor->GetName()) == nullptr)
                 return false;
        }
    }

    //setup new connection
    std::vector<Node *>& new_input=new_sub->input_nodes;

    for(unsigned int i=0;i<new_input.size();i++)
    {
        Node * input_node=new_input[i];

        for(unsigned int j=0;j<input_node->GetInputNum();j++)
        {
            NodePort * input_port=input_node->GetInputPort(j);
            Tensor *  tensor=input_port->tensor;

            if(tensor->GetType()==kConstTensor)
                  continue;

            tensor->AddConsumer(input_port);
        }
    }

    std::vector<Node *>& new_output=new_sub->output_nodes;

    for(unsigned int i=0;i<new_output.size();i++)
    {
        Node * output_node=new_output[i];

        for(unsigned int j=0;j<output_node->GetOutputNum();j++)
        {
            NodePort * output_port=output_node->GetOutputPort(j);
            Tensor *  tensor=output_port->tensor;

            NodePort * orig_producer=tensor->producer;
            Node * orig_node=orig_producer->owner;
            //forget the tensor
            orig_node->RemoveOutputPort(orig_producer->port_index);

            //the new producer
            tensor->producer=output_port;
        }
    }

    //removing nodes in whole graph
    for(unsigned int i=0;i<orig_sub->seq_nodes.size();i++)
    {
        Node * node=orig_sub->seq_nodes[i];
        RemoveNode(node);
    }

    //add nodes/tensors in new to whole graph
    for(unsigned int i=0;i<new_sub->seq_nodes.size();i++)
    {
        bool graph_output_node=false;
        bool graph_input_node=false;

        Node * node=new_sub->seq_nodes[i];

        if(new_sub->RemoveNodeOwner(node))
        {
           /* it is a new created  node */
           SetNodeOwner(node);
           seq_nodes.push_back(node);
          
           /* check if tensor produced are new or not */
           for(unsigned int j=0;j<node->GetOutputNum();j++)
           {
              Tensor * tensor=node->GetOutputTensor(j);

              if(new_sub->RemoveTensorOwner(tensor))
              {
                 SetTensorOwner(tensor);
                 tensor_map_[tensor->GetName()]=tensor;
              }

              if(tensor->consumer.size()==0)
                   graph_output_node=true;
           }

           if(node->GetInputNum()==0 )
           {
              Operator * op=node->GetOp();

              if(op->GetName()!="Const")
                  graph_input_node=true;
           }

           if(graph_input_node)
                input_nodes.push_back(node);
           
           if(graph_output_node)
                output_nodes.push_back(node);
        }
        else
        {
           XLOG_ERROR()<<"WHY GOES HERE!!!\n";
        }
    }


    //re-sort the graph

    SanitizeGraph();

    return true;
}

} //namespace TEngine
